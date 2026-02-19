#include "vmlinux.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <linux/errno.h>
#include <stdbool.h>
#include "bpf_lsm_policy.h"

char LICENSE[] SEC("license") = "GPL";

#define KVM_CREATE_VM 0xAE01

uint64_t vm_system_lock = 0;

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, bool);
} is_vm_owner SEC(".maps");

SEC("lsm/file_ioctl")
int BPF_PROG(restrict_kvm_create, struct file *file, unsigned int cmd,
	     unsigned long arg)
{
	struct task_struct *current = bpf_get_current_task_btf();
	bool *task_is_vm_owner;
	int vm_running;

	if (cmd != KVM_CREATE_VM)
		return 0;

	vm_running = __sync_val_compare_and_swap(&vm_system_lock, 0, 1);

	if (vm_running)
		return BPF_LSM_DECISION(
			-EPERM,
			"KVM_CREATE_VM: Denied. System-wide VM lock is active.\n");

	task_is_vm_owner = bpf_task_storage_get(&is_vm_owner, current, 0,
						BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (task_is_vm_owner) {
		*task_is_vm_owner = true;
		bpf_printk("KVM_CREATE_VM: Lock acquired by PID %d\n",
			   current->pid);
	}

	return 0;
}

SEC("lsm/task_free")
void BPF_PROG(release_vm_lock, struct task_struct *task)
{
	bool *owner = bpf_task_storage_get(&is_vm_owner, task, 0, 0);

	if (owner && *owner) {
		__sync_lock_test_and_set(&vm_system_lock, 0);
		bpf_printk(
			"KVM_CREATE_VM: Lock released (Task PID %d exited)\n",
			task->pid);
	}
}

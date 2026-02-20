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
/* Can be extended / generalized if there are more emulators
 * installed.
 */
unsigned long qemu_inode = 0;

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, bool);
} is_vm_owner SEC(".maps");

/* The policy intercepts at various phases of the VM creation life-cycle.
 * It's expected that the system has some form of trusted execution
 * and can restrict the number of "VM launchers".
 */
static __always_inline int try_acquire_vm_lock(void)
{
    struct task_struct *current = bpf_get_current_task_btf();
    bool *task_is_vm_owner;


    task_is_vm_owner = bpf_task_storage_get(&is_vm_owner, current, 0,
                        BPF_LOCAL_STORAGE_GET_F_CREATE);

    if (!task_is_vm_owner)
        return 0;

    /* The task already grabbed the global lock earlier in its life-cycle */
    if (*task_is_vm_owner)
        return 0;

    if (__sync_val_compare_and_swap(&vm_system_lock, 0, 1))
        return -EPERM;

    *task_is_vm_owner = true;
    return 0;
}

/* For KVM accelerated VMs, there's a stronger guarantee that does
 * not rely on incercepting emulator execution.
 */
SEC("lsm/file_ioctl")
int BPF_PROG(restrict_kvm_create, struct file *file, unsigned int cmd,
         unsigned long arg)
{
    int err;

    if (cmd != KVM_CREATE_VM)
        return 0;

    err = try_acquire_vm_lock();
    if (err)
        return BPF_LSM_DECISION(
            err,
            "KVM_CREATE_VM: Denied. System-wide VM lock is active.\n");

    return 0;
}

/* Emulated VMs look just like any user-space programs, these are restricted by
 * restricting the number of emulator instances that can be alive on the system.
 */
SEC("lsm/bprm_check_security")
int BPF_PROG(restrict_qemu, struct linux_binprm *bprm)
{
    uint64_t ino = bprm->file->f_inode->i_ino;
    int err;

    if (qemu_inode == 0 || ino != qemu_inode)
        return 0;

    err = try_acquire_vm_lock();
    if (err)
        return BPF_LSM_DECISION(
            err,
            "BPRM_CHECK: Denied. Software VM blocked by system lock.\n");

    return 0;
}

SEC("lsm/task_alloc")
int BPF_PROG(vm_task_alloc, struct task_struct *task, unsigned long clone_flags)
{
    struct task_struct *parent = bpf_get_current_task_btf();
	bool *is_parent_owner, *child_inherits;

	is_parent_owner = bpf_task_storage_get(&is_vm_owner, parent, 0, 0);
    if (is_parent_owner && *is_parent_owner) {
    	child_inherits = bpf_task_storage_get(&is_vm_owner, task, 0,
                                BPF_LOCAL_STORAGE_GET_F_CREATE);
        if (child_inherits)
            *child_inherits = true;
    }
    return 0;
}

SEC("lsm/task_free")
void BPF_PROG(release_vm_lock, struct task_struct *task)
{
	bool *owner = bpf_task_storage_get(&is_vm_owner, task, 0, 0);

	if (owner && *owner && task->pid == task->tgid) {
		__sync_lock_test_and_set(&vm_system_lock, 0);
		bpf_printk(
			"KVM_CREATE_VM: Lock released (Task PID %d exited)\n",
			task->pid);
	}
}

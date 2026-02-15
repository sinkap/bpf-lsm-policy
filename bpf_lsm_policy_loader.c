#include "vm.skel.h"
#include "restrict.skel.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PIN_PATH "/sys/fs/bpf/bpf_lsm_policy"

#define PIN_LINK(skel, member) ({								        \
	int __err = 0;										                \
	struct bpf_link *__link = (skel)->links.member;				        \
	const char *__path = PIN_PATH "/" #member;					        \
														                \
	if (!__link) {										                \
		fprintf(stderr, "Error: Link '%s' is NULL\n", #member);		    \
		__err = -EINVAL;								                \
	} else {											                \
		__err = bpf_link__pin(__link, __path);				            \
		if (__err) {									                \
			fprintf(stderr, "Error: Failed to pin '%s': %d\n",		    \
				#member, __err);						                \
		} else {										                \
			printf("Info: Pinned link '%s' -> %s\n", #member, __path);	\
		}												                \
	}													                \
	__err;												                \
})

static struct vm_bpf *vm_policy_init(void)
{
	struct vm_bpf *skel;
	int err;

	skel = vm_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr,
			"Error: Failed to open and load VM BPF skeleton\n");
		return NULL;
	}

	err = vm_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Error: Failed to attach VM BPF: %d (%s)\n",
			err, strerror(-err));
		goto cleanup;
	}

	if (PIN_LINK(skel, restrict_kvm_create))
		goto cleanup;

	if (PIN_LINK(skel, release_vm_lock))
		goto cleanup;

	return skel;

cleanup:
	bpf_link__unpin(skel->links.release_vm_lock);
	bpf_link__unpin(skel->links.restrict_kvm_create);
	vm_bpf__destroy(skel);
	return NULL;
}

static struct restrict_bpf *finalize_lsm_policy(void)
{
	struct restrict_bpf *skel;
	int err;

	skel = restrict_bpf__open();
	if (!skel) {
		fprintf(stderr,
			"Error: Failed to open Restrict BPF skeleton\n");
		return NULL;
	}

	/* We disable auto-attach for the restrictive prog so we can control order */
	bpf_program__set_autoattach(skel->progs.restrict_bpf_load, false);

	err = restrict_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Error: Failed to load Restrict BPF: %d\n",
			err);
		goto cleanup;
	}

	/* Attach normal policies (auto-attach enabled ones) */
	err = restrict_bpf__attach(skel);
	if (err) {
		fprintf(stderr,
			"Error: Failed to attach Restrict BPF: %d (%s)\n", err,
			strerror(-err));
		goto cleanup;
	}

	/* 1. Pin the shield (unlink restriction) */
	if (PIN_LINK(skel, restrict_inode_unlink))
		goto cleanup;

	skel->links.restrict_bpf_load =
		bpf_program__attach(skel->progs.restrict_bpf_load);
	if (!skel->links.restrict_bpf_load) {
		err = -errno;
		fprintf(stderr,
			"Error: Failed to manually attach restrict_bpf_load: %d\n",
			err);
		goto cleanup;
	}

	if (PIN_LINK(skel, restrict_bpf_load))
		goto cleanup;

	return skel;

cleanup:
	bpf_link__unpin(skel->links.restrict_inode_unlink);
	bpf_link__unpin(skel->links.restrict_bpf_load);
	restrict_bpf__destroy(skel);
	return NULL;
}

int main(int argc, char **argv)
{
	struct vm_bpf *vm_skel = NULL;
	struct restrict_bpf *restrict_skel = NULL;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (mkdir(PIN_PATH, 0755) != 0 && errno != EEXIST) {
		fprintf(stderr, "Error: Failed to create directory %s: %s\n",
			PIN_PATH, strerror(errno));
		return 1;
	}

	vm_skel = vm_policy_init();
	if (!vm_skel) {
		fprintf(stderr, "Fatal: VM policy initialization failed.\n");
		return 1;
	}

	restrict_skel = finalize_lsm_policy();
	if (!restrict_skel) {
		fprintf(stderr,
			"Fatal: LSM lockdown failed. Rolling back VM policies...\n");
		bpf_link__unpin(vm_skel->links.restrict_kvm_create);
		bpf_link__unpin(vm_skel->links.release_vm_lock);
		vm_bpf__destroy(vm_skel);
		return 1;
	}

	printf("\nSuccess: All LSM policies loaded, pinned, and system locked down.\n");

	vm_bpf__destroy(vm_skel);
	restrict_bpf__destroy(restrict_skel);

	return 0;
}
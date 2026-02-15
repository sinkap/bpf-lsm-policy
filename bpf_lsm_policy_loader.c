#include "vm.skel.h"
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

static int pin_link(struct bpf_link *link, const char *name)
{
    char path[PATH_MAX];
    int len, err;

    if (!link) {
        fprintf(stderr, "Error: Link '%s' is NULL. Did attachment fail?\n", name);
        return -EINVAL;
    }

    len = snprintf(path, sizeof(path), "%s/%s_link", PIN_PATH, name);
    if (len < 0 || len >= sizeof(path)) {
        fprintf(stderr, "Error: Path too long for link '%s'\n", name);
        return -ENAMETOOLONG;
    }

    err = bpf_link__pin(link, path);
    if (err) {
        fprintf(stderr, "Error: Failed to pin link '%s' to '%s': %d (%s)\n", 
                name, path, err, strerror(-err));
        return err;
    }

    printf("Info: Pinned link '%s' -> %s\n", name, path);
    return 0;
}

int main(int argc, char **argv)
{
    struct vm_bpf *skel = NULL;
    int err;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    skel = vm_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Error: Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = vm_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Error: Failed to attach BPF: %d (%s)\n", err, strerror(-err));
        goto cleanup;
    }

    err = bpf_object__pin(skel->obj, PIN_PATH);
    if (err) {
        fprintf(stderr, "Error: Failed to pin BPF object to %s: %d (%s)\n",
                PIN_PATH, err, strerror(-err));
        goto cleanup;
    }

    err = pin_link(skel->links.restrict_kvm_create, "restrict_kvm_create");
    if (err) {
        goto cleanup_pinned;
    }

    err = pin_link(skel->links.release_vm_lock, "release_vm_lock");
    if (err) {
        goto cleanup_pinned;
    }

    printf("Success: LSM policies loaded and pinned.\n");
    return 0;

cleanup_pinned:
    fprintf(stderr, "Error: Pinning failed, cleaning up...\n");
    bpf_object__unpin(skel->obj, PIN_PATH);

cleanup:
    vm_bpf__destroy(skel);
    return -err;
}

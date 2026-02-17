# BPF LSM Policy

This project implements a security policy using the Linux Security Module (LSM) framework with BPF (Berkeley Packet Filter). It provides a mechanism to enforce system-wide policies, demonstrated with a lock on KVM virtual machine creation.

## Description

The project consists of a user-space loader and BPF programs that are attached to LSM hooks. The primary goal is to load a set of security policies and then "lock down" the system to prevent these policies from being altered or unloaded.

### Components

*   **`bpf_lsm_policy_loader`**: The user-space application responsible for loading, attaching, and pinning the BPF programs. It creates a directory at `/sys/fs/bpf/bpf_lsm_policy` where the BPF links are pinned.
*   **`vm.bpf.c`**: A BPF program that enforces a system-wide lock on KVM virtual machine creation.
    *   `restrict_kvm_create`: Attached to the `lsm/file_ioctl` hook, it allows only one process to create a KVM VM. Once a process creates a VM, no other process can do so until the original process exits.
    *   `release_vm_lock`: Attached to the `lsm/task_free` hook, it releases the VM lock when the process that acquired it exits.
*   **`restrict.bpf.c`**: A BPF program designed to finalize the security policy and prevent tampering.
    *   `restrict_inode_unlink`: Attached to the `lsm/inode_unlink` hook, it prevents the unlinking (deletion) of pinned BPF LSM links from the bpffs filesystem. This makes the loaded LSM policies persistent until the next reboot.
    *   `restrict_bpf_load`: Attached to the `lsm/bpf` hook, it prevents any new BPF programs of type `BPF_PROG_TYPE_LSM` from being loaded, effectively locking the LSM policy.
*   **`bpf_lsm_policy_loader.service`**: A systemd service file to run the `bpf_lsm_policy_loader` at boot time, ensuring the policy is applied automatically.

### Policy Enforcement

The policy can run in two modes, controlled by the `BPF_LSM_POLICY_ENFORCE` environment variable:

*   **Permissive (Dry-Run)**: This is the default mode. The BPF programs will log policy violations (e.g., trying to create a second VM) via `bpf_printk`, but will not actually block the operation.
*   **Enforce**: To enable this mode, set the environment variable `BPF_LSM_POLICY_ENFORCE=1` before running the loader. In this mode, policy violations are actively blocked with an `EPERM` (Permission denied) error.

The `bpf_lsm_policy_loader.service` does not set this variable by default, so the policy will be in dry-run mode. To enable enforcement mode for the service, you can edit the service file to set the environment variable.

## Getting Started

### Prerequisites

*   A Linux kernel with BPF and LSM support.
*   The kernel must be compiled with `CONFIG_BPF_LSM=y`.
*   The bpffs filesystem must be mounted at `/sys/fs/bpf`.
*   `clang`, `libbpf-dev`, and `bpftool` must be installed.

### Building

To build the project, simply run `make`:

```bash
make
```

This will compile the BPF programs, generate the BPF skeletons, and build the `bpf_lsm_policy_loader` executable.

### Installation and Activation

To install the loader and the systemd service, run:

```bash
sudo make install
```

This will:
1.  Install the `bpf_lsm_policy_loader` binary to `/usr/local/sbin/`.
2.  Install the `bpf_lsm_policy_loader.service` file to `/etc/systemd/system/`.

To enable and start the service, run the `load` target:

```bash
sudo make load
```

This will reload the systemd daemon, enable the service to start on boot, and start it immediately. You can check the status with:

```bash
systemctl status bpf_lsm_policy_loader.service
```

### Uninstallation

To stop the service and remove the installed files, run:

```bash
sudo make uninstall
```

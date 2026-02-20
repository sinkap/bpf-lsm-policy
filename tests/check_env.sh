#!/bin/bash
set -e

# Pull in your beautiful color logger
source "$(dirname "$0")/common.sh"

echo "::group::BPF LSM Check"
ACTIVE_LSMS=$(cat /sys/kernel/security/lsm)
log_info "Found active LSMs: $ACTIVE_LSMS"
if [[ "$ACTIVE_LSMS" == *"bpf"* ]]; then
  log_success "BPF LSM is active."
else
  log_error "BPF LSM is missing."
  exit 1
fi
echo "::endgroup::"

echo "::group::Nested KVM Check"
if [ -c /dev/kvm ]; then
  log_success "/dev/kvm is present inside the VM!"
else
  log_error "/dev/kvm is missing inside the guest."
  exit 1
fi
echo "::endgroup::"

echo "::group::BTF vmlinux Check"
if [ -f /sys/kernel/btf/vmlinux ]; then
  log_success "BTF vmlinux is present at /sys/kernel/btf/vmlinux."
else
  log_error "BTF vmlinux is missing."
  exit 1
fi
echo "::endgroup::"


#!/bin/bash
set -ex

set -a
[ -f /etc/environment ] && source /etc/environment
set +a

echo "Active Enforcement Level: ${BPF_LSM_POLICY_ENFORCE:-0}"

KERNEL_URL="http://ftp.debian.org/debian/dists/bookworm/main/installer-amd64/current/images/netboot/debian-installer/amd64/linux"
KERNEL_FILE="vmlinuz-debian"

make
make install
make load

cleanup() {
    if [ -f vm1.pid ]; then
        kill $(cat vm1.pid) 2>/dev/null || true
        rm -f vm1.pid
    fi
    if [ -f vm2.pid ]; then
        kill $(cat vm2.pid) 2>/dev/null || true
        rm -f vm2.pid
    fi
    rm -f init.c init minimal_initrd.img vm1.img vm2.img vm1.log vm2.log
    make uninstall
}
trap cleanup EXIT SIGINT SIGTERM

if [ ! -f "$KERNEL_FILE" ]; then
    wget -q -O "$KERNEL_FILE" "$KERNEL_URL"
fi

cat <<EOF > init.c
#include <stdio.h>
#include <unistd.h>
int main() {
    printf("SUCCESS: The VM is alive!\n");
    fflush(stdout);
    while(1) { sleep(3600); }
    return 0;
}
EOF

clang -static -o init init.c
echo init | cpio -o --format=newc > minimal_initrd.img

dd if=/dev/zero of=vm1.img bs=1M count=1
dd if=/dev/zero of=vm2.img bs=1M count=1

echo "--- Starting First VM ---"
qemu-system-x86_64 -kernel "$KERNEL_FILE" -initrd minimal_initrd.img -enable-kvm \
    -append "console=ttyS0 rdinit=/init panic=0" -drive file=vm1.img,format=raw,index=0,media=disk \
    -display none -serial file:vm1.log -m 512 -daemonize -pidfile vm1.pid

if ! kill -0 $(cat vm1.pid) 2>/dev/null; then
    echo "ERROR: First VM failed to start!"
    cat vm1.log
    exit 1
fi
echo "First VM is running (PID: $(cat vm1.pid))."

echo "--- Starting Second VM ---"
set +e
timeout 5s qemu-system-x86_64 -kernel "$KERNEL_FILE" -initrd minimal_initrd.img -enable-kvm \
    -append "console=ttyS0 rdinit=/init panic=0" -drive file=vm2.img,format=raw,index=0,media=disk \
    -display none -serial file:vm2.log -m 512 -pidfile vm2.pid
EXIT_CODE=$?
set -e

if [ "${BPF_LSM_POLICY_ENFORCE:-0}" == "1" ]; then
    if [ $EXIT_CODE -eq 124 ] || [ $EXIT_CODE -eq 0 ]; then
        echo "TEST FAILED: Second VM was allowed to start, but LSM should have blocked it!"
        exit 1
    else
        echo "TEST PASSED: Second VM was successfully blocked by LSM (Exit Code: $EXIT_CODE)."
    fi
else
    if [ $EXIT_CODE -eq 124 ] || [ $EXIT_CODE -eq 0 ]; then
        echo "TEST PASSED: Second VM started successfully (No LSM interference)."
    else
        echo "TEST FAILED: Second VM failed to start, but LSM is OFF!"
        exit 1
    fi
fi

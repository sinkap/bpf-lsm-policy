#!/usr/bin/env bash
set -euxo pipefail

# Create the mount point
mkdir -p /host

config_path="/etc/fstab"

# Overwrite fstab to auto-mount the host directory on boot
cat > "$config_path" << 'EOF'
host_mount  /host  9p  trans=virtio,msize=512000,rw,nofail 0  0
/dev/root   /      ext4    errors=remount-ro   0   1
EOF

chmod 644 "$config_path"

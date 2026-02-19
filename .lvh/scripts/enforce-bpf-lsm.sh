#!/usr/bin/env bash
set -euxo pipefail

# 1. Systemd Native
mkdir -p /etc/systemd/system.conf.d
cat > /etc/systemd/system.conf.d/10-default-env.conf << 'EOF'
[Manager]
DefaultEnvironment="BPF_LSM_POLICY_ENFORCE=1"
EOF

# 2. Global Environment
echo "BPF_LSM_POLICY_ENFORCE=1" >> /etc/environment
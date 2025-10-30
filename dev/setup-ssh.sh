#!/usr/bin/env bash
set -e

mkdir -p /root/.ssh

cat > /root/.ssh/config << 'EOF'
Host github.com-portilloromano
  HostName github.com
  User git
  IdentityFile /root/.ssh/id_personal
  IdentitiesOnly yes
EOF

chmod 700 /root/.ssh
chmod 600 /root/.ssh/config

if [ -f /root/.ssh/id_personal ]; then
  chmod 600 /root/.ssh/id_personal
fi

if [ -f /root/.ssh/id_personal.pub ]; then
  chmod 644 /root/.ssh/id_personal.pub
fi

chown -R root:root /root/.ssh

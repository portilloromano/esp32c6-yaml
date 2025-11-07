#!/usr/bin/env bash
set -e

VENV=/opt/espressif/tools/python_env/idf5.4_py3.12_env

if ! grep -q "^export PATH=\"${VENV}/bin:\$PATH\"$" ~/.bashrc 2>/dev/null; then
  echo "export PATH=\"${VENV}/bin:\$PATH\"" >> ~/.bashrc
fi

if ! grep -q "esp-idf/export.sh" ~/.bashrc 2>/dev/null; then
  echo "source $IDF_PATH/export.sh" >> ~/.bashrc
fi

if ! grep -q "^export CHIP_ROOT=" ~/.bashrc 2>/dev/null; then
  echo "export CHIP_ROOT=$CHIP_ROOT" >> ~/.bashrc
fi

if ! grep -q "^export ZAP_GENERATED_ROOT=" ~/.bashrc 2>/dev/null; then
  echo "export ZAP_GENERATED_ROOT=$ZAP_GENERATED_ROOT" >> ~/.bashrc
fi

[ -L /zzz_generated ] || ln -s "$ZAP_GENERATED_ROOT" /zzz_generated || true

chmod +x .devcontainer/postStart.sh

#!/usr/bin/env bash
set -e

echo "=== asr-kb setup ==="

# 1. Python virtual environment (recommended)
echo ""
echo "A Python virtual environment is recommended to avoid conflicts with system packages."
read -rp "Create Python virtual environment? (recommended) [Y/n]: " venv_choice
venv_choice="${venv_choice,,}"

if [[ "$venv_choice" != "n" ]]; then
    echo "Creating Python virtual environment..."
    sudo python3 -m venv /opt/asr-kb/venv
    echo "Installing Python dependencies..."
    sudo /opt/asr-kb/venv/bin/pip install -r requirements.txt
    echo "Done."
else
    echo ""
    echo "WARNING: Installing dependencies system-wide can break existing Python packages."
    echo "Use at your own risk."
    read -rp "Install Python dependencies system-wide? [y/N]: " deps_choice
    deps_choice="${deps_choice,,}"
    
    if [[ "$deps_choice" == "y" ]]; then
        echo "Installing Python dependencies system-wide..."
        pip3 install --break-system-packages -r requirements.txt
        echo "Done."
    else
        echo "Skipping Python dependencies."
    fi
fi

# 2. Build and install
echo ""
echo "Building and installing..."
cmake . && make -j 32 && sudo make install
echo ""
echo "=== setup complete ==="
echo "Run: asr-kb"

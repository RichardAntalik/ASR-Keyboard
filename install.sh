#!/usr/bin/env bash
set -e

echo "=== asr-kb deployment ==="

# Create venv
if [ ! -d "asr-env" ]; then
    python3 -m venv asr-env
    echo "Created venv: asr-env"
else
    echo "Venv already exists: asr-env"
fi

# Activate venv
source asr-env/bin/activate

# Install dependencies
pip install --upgrade pip
pip install -r requirements.txt

echo "=== dependencies installed ==="
echo "Run asr-server.py to start the server"
echo "Run asr-kb.py to start the client"

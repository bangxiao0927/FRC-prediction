#!/usr/bin/env bash
# Deploy FRC Prediction on Ubuntu 22.04 / 24.04
# Run as the user who will own the service (not root).
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="${APP_DIR:-$(cd -- "$SCRIPT_DIR/.." && pwd)}"
REPO_URL="https://github.com/bangxiao0927/FRC-prediction.git"

echo "=== 1. Install system packages ==="
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential cmake git curl zip unzip tar pkg-config \
  python3 python3-pip python3-venv

echo "=== 2. Install vcpkg ==="
if [ ! -d "$HOME/vcpkg" ]; then
  git clone --depth 1 https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
  "$HOME/vcpkg/bootstrap-vcpkg.sh"
fi
export VCPKG_ROOT="$HOME/vcpkg"

# Persist VCPKG_ROOT in shell profile
if ! grep -q 'VCPKG_ROOT' "$HOME/.bashrc" 2>/dev/null; then
  echo "export VCPKG_ROOT=$HOME/vcpkg" >> "$HOME/.bashrc"
fi

echo "=== 3. Clone repo ==="
if [ ! -d "$APP_DIR" ]; then
  git clone "$REPO_URL" "$APP_DIR"
elif [ ! -d "$APP_DIR/.git" ]; then
  echo "$APP_DIR exists but is not a git repository." >&2
  exit 1
fi
cd "$APP_DIR"
git pull --ff-only origin main

echo "=== 4. Build C++ CLI ==="
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc)"

echo "=== 5. Python environment ==="
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install -r requirements.txt

echo "=== 6. Configure TBA key ==="
if [ ! -f config.json ]; then
  cp config.example.json config.json
  echo ">> Edit config.json and set your tba_auth_key"
fi

echo "=== 7. Install systemd service ==="
sudo cp deploy/frc-prediction.service /etc/systemd/system/
sudo sed -i "s|%USER%|$USER|g" /etc/systemd/system/frc-prediction.service
sudo sed -i "s|%APP_DIR%|$APP_DIR|g" /etc/systemd/system/frc-prediction.service
sudo systemctl daemon-reload
sudo systemctl enable frc-prediction
sudo systemctl restart frc-prediction

echo "=== Done ==="
echo "Dashboard: http://$(hostname -I | awk '{print $1}')"
echo "Check status: sudo systemctl status frc-prediction"
echo "Logs: sudo journalctl -u frc-prediction -f"

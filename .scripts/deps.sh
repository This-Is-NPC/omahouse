#!/usr/bin/env bash
# Check system build deps. Idempotent: same machine, same result.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

missing=""
for tool in qmake6 g++ make; do
  command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
for header in QtCore/QCoreApplication QtTest/QtTest; do
  ls /usr/include/qt6/$header >/dev/null 2>&1 || missing="$missing $header"
done

if [ -z "$missing" ]; then
  echo "all present — qt $(qmake6 -query QT_VERSION), $(g++ --version | head -1)"
  exit 0
fi

echo "missing:$missing" >&2
echo >&2
echo "on Arch / Omarchy:" >&2
echo "  sudo pacman -S --needed qt6-base gcc make" >&2
exit 1

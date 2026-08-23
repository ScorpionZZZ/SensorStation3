#!/usr/bin/env bash
#
# Builds the project and produces a single merged flash image at
# build/SensorStation3-merged.bin — bootloader, partition table, otadata,
# and app combined into one file flashable at offset 0x0 (e.g. for
# distributing to end users via esptool or a web-based flasher that only
# accepts a single binary).
#
# Usage:
#   scripts/merge-bin.sh
#
# Uses `idf.py` from PATH if you've already sourced ESP-IDF's export.sh
# (the standard setup — see README.md). Falls back to this maintainer's
# local multi-version IDF install otherwise (see CLAUDE.md's Build section
# for why: export.sh's venv there doesn't match the configured build dir).

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

if command -v idf.py >/dev/null 2>&1; then
  idf.py merge-bin -o SensorStation3-merged.bin
else
  IDF_PATH=/Users/scorpionzzz/.espressif/v6.0.2/esp-idf
  ESP_IDF_VERSION=6.0.2
  IDF_PY="$IDF_PATH/tools/idf.py"
  PYTHON=/Users/scorpionzzz/.espressif/tools/python/v6.0.2/venv/bin/python
  TOOLCHAIN_BIN=/Users/scorpionzzz/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin

  # merge-bin implicitly builds the project first; the toolchain is added to
  # PATH so this also works after CMakeLists changes (new components etc.),
  # which force CMake to re-run and need the compiler in PATH.
  PATH="$TOOLCHAIN_BIN:$PATH" IDF_PATH="$IDF_PATH" ESP_IDF_VERSION="$ESP_IDF_VERSION" \
    "$PYTHON" "$IDF_PY" merge-bin -o SensorStation3-merged.bin
fi

echo "merged binary: build/SensorStation3-merged.bin"
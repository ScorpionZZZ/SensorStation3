#!/usr/bin/env bash
#
# Builds the project and produces, in build/:
#   SensorStation3.bin                     — plain app image
#   SensorStation3-merged.bin              — bootloader + parts + otadata + app,
#                                            flashable at offset 0x0
#   SensorStation3-v<X.Y.Z>.bin            — versioned copy of the app image
#   SensorStation3-v<X.Y.Z>-merged.bin     — versioned copy of the merged image
#
# The version comes from APP_VERSION_{MAJOR,MINOR,BUILD} in ./CMakeLists.txt.
#
# Usage:
#   scripts/merge-bin.sh
#
# Uses `idf.py` from PATH if you've already sourced ESP-IDF's export.sh
# (the standard setup — see README.md). Otherwise it pulls the environment
# from this maintainer's per-version activation script
# (~/.espressif/tools/activate_idf_v<version>.sh -e) — see CLAUDE.md's Build
# section for why export.sh's venv there doesn't match the configured build dir.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

PROJECT_NAME="SensorStation3"

# ── Project version, from CMakeLists.txt ───────────────────────────────────
cmake_ver_field() {
  sed -n "s/^set(APP_VERSION_$1 \([0-9][0-9]*\)).*/\1/p" CMakeLists.txt
}
V_MAJOR="$(cmake_ver_field MAJOR)"
V_MINOR="$(cmake_ver_field MINOR)"
V_BUILD="$(cmake_ver_field BUILD)"
if [ -z "$V_MAJOR" ] || [ -z "$V_MINOR" ] || [ -z "$V_BUILD" ]; then
  echo "error: could not read APP_VERSION_* from CMakeLists.txt" >&2
  exit 1
fi
VERSION="${V_MAJOR}.${V_MINOR}.${V_BUILD}"

# ── Resolve an `idf.py` runner ────────────────────────────────────────────
if command -v idf.py >/dev/null 2>&1; then
  run_idf() { idf.py "$@"; }
else
  # Full x.y.z version — also the activation-script suffix. Without a concrete
  # ESP_IDF_VERSION the component-manager CLI extension crashes with a
  # Version.coerce(None) TypeError (see CLAUDE.md's Build section).
  ESP_IDF_VERSION="${ESP_IDF_VERSION:-6.0.2}"
  ACTIVATE="${IDF_TOOLS_PATH:-$HOME/.espressif/tools}/activate_idf_v${ESP_IDF_VERSION}.sh"

  if [ ! -x "$ACTIVATE" ]; then
    echo "error: ESP-IDF activation script not found: $ACTIVATE" >&2
    echo "source ESP-IDF's export.sh first, or set ESP_IDF_VERSION to an installed version." >&2
    exit 1
  fi

  # `activate_idf_v<ver>.sh -e` prints the full ESP-IDF environment as
  # KEY=VALUE lines (PATH, SYSTEM_PATH, IDF_PATH, IDF_PYTHON_ENV_PATH, …)
  # without needing to be sourced — this is how we get the toolchain + venv
  # paths without hardcoding them here.
  system_path=""
  idf_path_entries=""
  while IFS='=' read -r key value; do
    [ -n "$key" ] || continue
    case "$key" in
      SYSTEM_PATH)     system_path="$value" ;;
      PATH)            idf_path_entries="$value" ;;
      ESP_IDF_VERSION) ;;  # keep our full x.y.z; -e only reports x.y
      *)               export "$key=$value" ;;
    esac
  done < <("$ACTIVATE" -e)

  export PATH="${idf_path_entries}:${system_path}"
  export ESP_IDF_VERSION

  run_idf() { "${IDF_PYTHON_ENV_PATH}/bin/python" "${IDF_PATH}/tools/idf.py" "$@"; }
fi

# ── Build, then merge, versioning a copy of each image ────────────────────
run_idf build
cp "build/${PROJECT_NAME}.bin" "build/${PROJECT_NAME}-v${VERSION}.bin"

# merge-bin implicitly builds first (a no-op now); the toolchain is on PATH via
# the activation env above, so this also works after CMakeLists changes (new
# components etc.), which force CMake to re-run and need the compiler in PATH.
run_idf merge-bin -o "${PROJECT_NAME}-merged.bin"
cp "build/${PROJECT_NAME}-merged.bin" "build/${PROJECT_NAME}-v${VERSION}-merged.bin"

echo "app binary:      build/${PROJECT_NAME}.bin"
echo "                 build/${PROJECT_NAME}-v${VERSION}.bin"
echo "merged binary:   build/${PROJECT_NAME}-merged.bin"
echo "                 build/${PROJECT_NAME}-v${VERSION}-merged.bin"
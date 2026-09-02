#!/usr/bin/env bash
# CommonwealthMP root bootstrap (WSL / macOS / Linux helpers).
# Plugin build is Windows-only; this still fetches git submodules and the Flex
# toolchain used by interface/swf/build.ps1.
#
# Usage:
#   ./setup.sh              submodules + Flex tools if missing
#   ./setup.sh --deps-only  submodules only
#   ./setup.sh --flex-only  Flex tools only
#   ./setup.sh --force-flex re-download / re-extract Flex tools

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

DO_DEPS=1
DO_FLEX=1
FORCE_FLEX=0

usage() {
  echo "Usage: ./setup.sh [--deps-only | --flex-only | --force-flex]" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --deps-only) DO_FLEX=0 ;;
    --flex-only) DO_DEPS=0 ;;
    --force-flex) FORCE_FLEX=1 ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1" >&2; usage ;;
  esac
  shift
done

setup_deps() {
  echo "==> git submodules"
  command -v git >/dev/null || { echo "ERROR: git not found on PATH." >&2; exit 1; }
  git submodule update --init --recursive
}

setup_flex() {
  local tools="$ROOT/interface/swf/_tools"
  local flex="$tools/adobe-flex"
  local pg_dir="$tools/player/32.0"
  local pg="$pg_dir/playerglobal.swc"
  local zip="$tools/apache-flex-sdk-4.16.1-bin.zip"
  local flex_url="https://archive.apache.org/dist/flex/4.16.1/binaries/apache-flex-sdk-4.16.1-bin.zip"
  local pg_url="https://fpdownload.macromedia.com/get/flashplayer/updaters/32/playerglobal32_0.swc"

  if [[ "$FORCE_FLEX" -eq 1 && -d "$flex" ]]; then
    echo "==> removing existing Flex SDK at $flex"
    rm -rf "$flex"
  fi

  if [[ -f "$flex/lib/mxmlc.jar" && -f "$pg" && "$FORCE_FLEX" -eq 0 ]]; then
    echo "==> Flex toolchain already present"
    echo "    SDK: $flex"
    echo "    playerglobal: $pg"
    return 0
  fi

  echo "==> Flex toolchain -> $tools"
  mkdir -p "$tools"

  if [[ ! -f "$flex/lib/mxmlc.jar" ]]; then
    if [[ ! -f "$zip" ]]; then
      echo "    downloading Apache Flex SDK 4.16.1 ..."
      curl -fL --retry 3 -o "$zip" "$flex_url"
    fi
    echo "    extracting to $flex ..."
    rm -rf "$flex"
    mkdir -p "$flex"
    tar -xf "$zip" -C "$flex" --strip-components=1
    [[ -f "$flex/lib/mxmlc.jar" ]] || {
      echo "ERROR: mxmlc.jar missing after extract." >&2
      exit 1
    }
  fi

  if [[ ! -f "$pg" ]]; then
    echo "    downloading playerglobal.swc (Flash Player 32) ..."
    mkdir -p "$pg_dir"
    curl -fL --retry 3 -o "$pg" "$pg_url"
  fi

  echo "    Flex SDK ready: $flex"
  echo "    playerglobal: $pg"
  echo "    Optional: CMP_FLEX_SDK / CMP_PLAYERGLOBAL / CMP_JAVA override paths."
  echo "    Java 8-21 required for interface/swf/build.ps1"
}

[[ "$DO_DEPS" -eq 1 ]] && setup_deps
[[ "$DO_FLEX" -eq 1 ]] && setup_flex

echo
echo "Setup complete."
echo "  Plugin (Windows): cd plugin && xmake f -m releasedbg -a x64 && xmake build"
echo "  Menu SWF:         cd interface/swf && powershell -ExecutionPolicy Bypass -File ./build.ps1"
echo "  Pack:             powershell -ExecutionPolicy Bypass -File scripts/pack-mo2.ps1 -Build"

#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PRESET="test"
while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--preset)
      [[ $# -ge 2 ]] || { echo "Error: $1 requires a preset name" >&2; exit 1; }
      PRESET="$2"
      shift 2
      ;;
    --preset=*)
      PRESET="${1#*=}"
      shift
      ;;
    *)
      echo "Error: Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

build_type="Debug"
enable_asan="OFF"
enable_clang_tidy="OFF"

case "$PRESET" in
  debug)
    ;;
  release)
    build_type="Release"
    ;;
  asan)
    enable_asan="ON"
    ;;
  quality|test)
    enable_asan="ON"
    enable_clang_tidy="ON"
    ;;
  *)
    echo "Error: Unsupported preset '$PRESET' for consumer validation" >&2
    exit 1
    ;;
esac

run_consumer_case() {
  local library_type="$1"
  local consumer_dir="$2"
  local consumer_target="$3"
  local runner_name="$4"
  local build_root="$REPO_ROOT/build/consumer-validation/${PRESET}/${library_type,,}"
  local install_prefix="$build_root/install"
  local consumer_build_dir="$build_root/${runner_name}-build"

  rm -rf "$build_root"

  cmake -S "$REPO_ROOT" -B "$build_root/project" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DFILTER_CHAIN_LIBRARY_TYPE="$library_type" \
    -DFILTER_CHAIN_BUILD_TESTS=OFF \
    -DENABLE_ASAN="$enable_asan" \
    -DENABLE_CLANG_TIDY="$enable_clang_tidy"
  cmake --build "$build_root/project"
  cmake --install "$build_root/project" --prefix "$install_prefix"

  cmake -S "$consumer_dir" -B "$consumer_build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_PREFIX_PATH="$install_prefix;${CMAKE_PREFIX_PATH:-}"
  cmake --build "$consumer_build_dir"

  if [[ "$library_type" == "SHARED" ]]; then
    LD_LIBRARY_PATH="$install_prefix/lib:${LD_LIBRARY_PATH:-}" "$consumer_build_dir/$consumer_target"
  else
    "$consumer_build_dir/$consumer_target"
  fi
}

run_consumer_case STATIC "$REPO_ROOT/tests/consumer/static" static_consumer static
run_consumer_case SHARED "$REPO_ROOT/tests/consumer/shared" shared_consumer shared
run_consumer_case STATIC "$REPO_ROOT/tests/consumer/c_api" c_api_consumer c-api-static
run_consumer_case SHARED "$REPO_ROOT/tests/consumer/c_api" c_api_consumer c-api-shared

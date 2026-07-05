#!/bin/sh

JOBS=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 2)

# merge_libgpmc: gpmc_core's own archive already contains the vendored
# glucose/cadical/flowcutter object files (folded in via OBJECT libraries in
# CMakeLists.txt), so publishing lib/libgpmc.a is just a copy. Library users
# link with -lgpmc (GMP stays a separate system dependency: -lgmpxx -lgmp).
merge_libgpmc() {
  mkdir -p lib \
  && cp -p build/libgpmc_core.a lib/libgpmc.a
}

# do_build <label> <extra-cmake-args...> -- <pp-flag>
# Configures, builds, and copies binaries into bin/. gpmc-pp is built and copied
# only when the caller passes "pp" as the second positional arg to the script.
do_build() {
  label="$1"; shift
  # Always pass the flag explicitly so it overrides any cached value from a
  # previous run (otherwise a stale ON would keep building gpmc-pp).
  if [ "$WITH_PP" = "pp" ]; then
    pp_args="-DGPMC_BUILD_PP=ON"
    label="$label (with gpmc-pp)"
  else
    pp_args="-DGPMC_BUILD_PP=OFF"
    # Drop any gpmc-pp left over from an earlier "pp" build so bin/ reflects
    # what was actually requested.
    rm -f build/gpmc-pp bin/gpmc-pp
  fi
  echo "Building $label..."
  cmake -B build -S . "$@" $pp_args \
  && cmake --build build --parallel "$JOBS" \
  && mkdir -p bin \
  && cp -p build/gpmc bin/ \
  && { [ -f build/gpmc-pp ] && cp -p build/gpmc-pp bin/ || true; } \
  && merge_libgpmc
}

WITH_PP="$2"

case "$1" in
  d)
    do_build "Debug" -DCMAKE_BUILD_TYPE=Debug
    ;;
  r)
    do_build "Release" -DCMAKE_BUILD_TYPE=Release
    ;;
  rs)
    do_build "Release Static" -DCMAKE_BUILD_TYPE=Release -DGPMC_STATIC_BUILD=on
    ;;
  clean)
    echo "Cleaning..."
    for target in build bin lib; do
      if [ -e "$target" ]; then
        rm -rf "$target"
        echo "  Removed $target/"
      else
        echo "  $target/ not found, skipping"
      fi
    done
    ;;
  *)
    echo "Usage: $0 {d|r|rs|clean} [pp]"
    echo "  d     Debug build"
    echo "  r     Release build"
    echo "  rs    Release static build"
    echo "  clean Remove build/, bin/, and lib/"
    echo ""
    echo "  pp    Append to d/r/rs to also build the standalone gpmc-pp"
    echo "        preprocessor (developers/researchers); copied into bin/ too."
    echo ""
    echo "Every build also merges gpmc_core + glucose + cadical + flowcutter"
    echo "into lib/libgpmc.a (link with -I include -L lib -lgpmc -lgmpxx -lgmp)."
    exit 1
    ;;
esac

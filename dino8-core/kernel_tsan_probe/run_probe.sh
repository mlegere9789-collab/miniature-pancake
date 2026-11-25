#!/bin/bash
# Builds the dino8-kernel, openNURBS and Manifold sources with ThreadSanitizer, links probe.cpp against them and runs it.
# Nothing in the repository's own build tree is touched; everything goes to $KT_OUT (about 150 MB).
#
# usage: run_probe.sh [probe arguments]        (default: --threads 4 --iters 200 --phase both)
# environment:
#   KT_DEPS   the `_deps` directory of an existing, configured dino8-kernel CMake build: it holds the fetched openNURBS
#             and Manifold sources and their prebuilt static libraries, whose member lists decide which sources are
#             rebuilt (default: dino8-kernel/build/_deps)
#   KT_OUT    work directory (default /tmp/dino8_kprobe)
#   KT_JOBS   parallel compile jobs (default 2; the shared build machine has 4 cores)
#   KT_PATCH  1 = rebuild two openNURBS files from a patched COPY (opennurbs-8.34-thread-safety.patch); default 0, i.e.
#             test the library exactly as the kernel builds it
#   KT_SUPP   path of a ThreadSanitizer suppressions file (default none)
#
# Every kernel and dependency translation unit is rebuilt with -fsanitize=thread -O1, so races inside openNURBS and
# Manifold are visible, not only races in kernel code. Reports go to $KT_OUT/tsan/ (one file per process).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
KERNEL=$REPO/dino8-kernel
DEPS=${KT_DEPS:-$KERNEL/build/_deps}
OUT=${KT_OUT:-/tmp/dino8_kprobe}
JOBS=${KT_JOBS:-2}
ON=$DEPS/opennurbs-src
MF=$DEPS/manifold-src
LIBON=$DEPS/opennurbs-build/libopennurbsStatic.a
LIBZ=$DEPS/opennurbs-build/zlib/libzlib.a
LIBUU=$DEPS/opennurbs-build/android_uuid/libandroid_uuid.a
LIBMF=$DEPS/manifold-build/src/libmanifold.a
for f in "$ON" "$MF" "$LIBON" "$LIBZ" "$LIBUU" "$LIBMF"; do
  [ -e "$f" ] || { echo "missing $f - point KT_DEPS at the _deps directory of a configured dino8-kernel build"; exit 2; }
done

if [ "${KT_PATCH:-0}" = 1 ]; then
  rm -rf "$OUT/on_patched"
  mkdir -p "$OUT/on_patched"
  cp "$ON"/opennurbs_revsurface.cpp "$ON"/opennurbs_statics.cpp "$OUT/on_patched/"
  (cd "$OUT/on_patched" && patch -p1 -s < "$HERE/opennurbs-8.34-thread-safety.patch")
fi

SAN="-fsanitize=thread -fno-omit-frame-pointer"
ONINC="-I$ON/. -I$ON/zlib/. -I$ON/android_uuid/."
MFINC="-I$MF/include -I$DEPS/manifold-build/include"
KINC="-I$KERNEL/include -I$KERNEL/include/.."
mkdir -p "$OUT/obj" "$OUT/tsan"

compile_list() {  # stdin: "<kind> <source>" lines
  xargs -P "$JOBS" -L1 bash -c '
    kind=$0; src=$1; n=$(basename "$src"); n=${n%.*}; obj='"$OUT"'/obj/${kind}_$n.o
    [ -f "$obj" ] && [ "$obj" -nt "$src" ] && exit 0
    case $kind in
      kernel) nice -n 10 c++ -O1 -g1 -DMANIFOLD_PAR=-1 -std=gnu++17 '"$SAN $KINC $ONINC $MFINC"' -c "$src" -o "$obj.tmp" ;;
      on)     nice -n 10 c++ -O1 -g0 -DNDEBUG -DMY_ZCALLOC -DON_CMAKE_BUILD -DON_COMPILING_OPENNURBS -DZ_PREFIX -Wno-switch -std=gnu++17 '"$SAN $ONINC"' -c "$src" -o "$obj.tmp" ;;
      mf)     nice -n 10 c++ -O1 -g0 -DNDEBUG -DMANIFOLD_PAR=-1 -std=c++17 -fPIC -ffp-contract=off -fexcess-precision=standard -Wno-unused -Wno-sign-compare '"$SAN $MFINC"' -c "$src" -o "$obj.tmp" ;;
      zlib)   nice -n 10 cc -O1 -g0 -DNDEBUG -DMY_ZCALLOC -DZ_PREFIX -fPIC '"$SAN"' -I'"$ON"'/zlib/. -c "$src" -o "$obj.tmp" ;;
      uuid)   nice -n 10 cc -O1 -g0 -DNDEBUG -fPIC '"$SAN"' -I'"$ON"'/android_uuid/. -c "$src" -o "$obj.tmp" ;;
    esac && mv -f "$obj.tmp" "$obj" && echo "built $kind $n"'
}

{
  for f in "$KERNEL"/src/*.cpp; do echo "kernel $f"; done
  # the source list of each dependency is the member list of the library the kernel build already links
  for m in $(ar t "$LIBON"); do
    b=${m%.o}
    if [ "${KT_PATCH:-0}" = 1 ] && [ -f "$OUT/on_patched/$b" ]; then echo "on $OUT/on_patched/$b"; else echo "on $ON/$b"; fi
  done
  for m in $(ar t "$LIBMF"); do echo "mf $MF/src/${m%.o}"; done
  for m in $(ar t "$LIBZ"); do echo "zlib $ON/zlib/${m%.o}"; done
  for m in $(ar t "$LIBUU"); do echo "uuid $ON/android_uuid/${m%.o}"; done
} | compile_list

# kernel objects are linked directly; the dependencies as one archive, so only the members the kernel needs are pulled in
rm -f "$OUT/libdeps_tsan.a"
ar rcs "$OUT/libdeps_tsan.a" "$OUT"/obj/on_*.o "$OUT"/obj/mf_*.o "$OUT"/obj/zlib_*.o "$OUT"/obj/uuid_*.o
c++ -O1 -g1 -DMANIFOLD_PAR=-1 -std=gnu++17 $SAN $KINC $ONINC $MFINC "$HERE/probe.cpp" "$OUT"/obj/kernel_*.o \
    -Wl,--start-group "$OUT/libdeps_tsan.a" -Wl,--end-group -o "$OUT/probe_tsan"
echo "linked $OUT/probe_tsan"

ARGS=("$@")
[ ${#ARGS[@]} -gt 0 ] || ARGS=(--threads 4 --iters 200 --phase both)
export TMPDIR=${TMPDIR:-$OUT/tsan}
export TSAN_OPTIONS="halt_on_error=0 exitcode=0 history_size=4 log_path=$OUT/tsan/report ${KT_SUPP:+suppressions=$KT_SUPP print_suppressions=1}"
"$OUT/probe_tsan" "${ARGS[@]}"
echo "ThreadSanitizer warnings in this directory so far: $(cat "$OUT"/tsan/report.* 2>/dev/null | grep -c 'WARNING: ThreadSanitizer') (files in $OUT/tsan/)"

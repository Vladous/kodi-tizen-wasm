#!/bin/bash
# Builds Kodi's binary add-ons for Samsung Tizen TVs into an add-on repository that a TV's
# Kodi installs them from, as a directory to serve over HTTP(S).
#
#   build-repository.sh <repository URL> <output directory> [add-on ...]
#
# Without add-ons, all binary add-ons listed in addons.txt are built. Needs the depends of
# a Tizen build (tools/depends configured with --with-platform=tizen and built), and the
# toolchain's strip on the PATH or in STRIP.
set -euo pipefail

if [ $# -lt 2 ]; then
  sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
fi

URL=${1%/}
OUT=$(mkdir -p "$2" && cd "$2" && pwd)
shift 2

HERE=$(cd "$(dirname "$0")" && pwd)
KODI=$(cd "$HERE/../../.." && pwd)
if [ $# -gt 0 ]; then
  ADDONS="$*"
else
  ADDONS=$(grep -v '^#' "$HERE/addons.txt" | xargs)
fi

# A build directory of its own: the add-ons a TV's Kodi comes with are built into the
# depends instead, from where the app's package takes them.
BUILD=tizen-repository
cd "$KODI/tools/depends/target/binary-addons"
# Zips of earlier versions of the add-ons built now would end up in the repository too.
for addon in $ADDONS; do
  rm -rf "$OUT/.zips/$addon+"*
done
rm -f ".installed-$BUILD" "$KODI/cmake/addons/.success" "$KODI/cmake/addons/.failure"
make -j"${JOBS:-$(getconf _NPROCESSORS_ONLN)}" PLATFORM=$BUILD PACKAGE=1 ADDONS="$ADDONS" \
  INSTALL_PREFIX="$OUT/.install -DOVERRIDE_PATHS=ON" \
  EXTRA_CMAKE_ARGS="-DPACKAGE_ZIP=ON -DPACKAGE_DIR=$OUT/.zips -DADDON_PATCH_DIR=$HERE/patches"

if [ -f "$KODI/cmake/addons/.failure" ]; then
  echo "Add-ons that did not build: $(xargs < "$KODI/cmake/addons/.failure")"
fi

STRIP=${STRIP:-$(grep -m1 '^CMAKE_STRIP' "$BUILD/CMakeCache.txt" 2>/dev/null | cut -d= -f2)}
python3 -I "$HERE/make-repository.py" --url "$URL" --zips "$OUT/.zips" --out "$OUT" \
  --strip "${STRIP:-strip}" --repository "$HERE/repository.kodi-tizen"

#!/usr/bin/env bash
# install.sh -- deploy the built module to a Move over SSH.
#
#   DEVICE=ableton@move.local ./scripts/install.sh
#
# THE TARBALL IS WRAPPED IN A moonivibe/ FOLDER. Schwung Manager unpacks the
# archive and looks for a directory matching the module id inside it. A flat
# tarball installs over scp but fails from the manager with
# "no module directory found in tarball".
#
# STALE .so FILES ARE REMOVED BEFORE EXTRACTING. During debugging the device
# ended up with both dsp.so and moonivibe.so in the module directory. The host
# scans that directory, and extra shared objects there are at best confusing
# and at worst ambiguous. Clearing them makes the directory match exactly what
# this script deploys.

set -euo pipefail
cd "$(dirname "$0")/.."

DEVICE="${DEVICE:-ableton@move.local}"
TARGET="/data/UserData/schwung/modules/audio_fx/moonivibe"
PKG="build/pack"
SO="moonivibe.so"

if [ ! -f "$PKG/$SO" ]; then
  echo "no $PKG/$SO -- run scripts/build.sh first" >&2
  exit 1
fi

export COPYFILE_DISABLE=1

echo "packaging..."
rm -rf build/stage && mkdir -p build/stage/moonivibe
cp "$PKG/$SO" "$PKG/module.json" build/stage/moonivibe/
tar -czf build/moonivibe-module.tar.gz -C build/stage moonivibe

echo "copying to $DEVICE..."
scp build/moonivibe-module.tar.gz "$DEVICE:/data/UserData/"

echo "unpacking into $TARGET..."
ssh "$DEVICE" "mkdir -p '$TARGET' && rm -f '$TARGET'/*.so '$TARGET'/_* && tar -xzf /data/UserData/moonivibe-module.tar.gz -C '$TARGET' --strip-components=1"

echo "done. reboot the Move and pick MooniVibe in a Signal Chain audio-FX slot."

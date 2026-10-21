#!/usr/bin/env bash
# build.sh -- cross-compile moonivibe.so for Move (aarch64) in Docker.
#
# Mirrors junologue-chorus and nam:
#   -Ofast -shared -fPIC -march=armv8-a -mtune=cortex-a72 -DNDEBUG
# No -Wall -Wextra: the reference builds do not use them, and strict warnings
# turn a stray character in a comment into a build failure.
#
# The output is named AFTER THE MODULE. Inside Signal Chain the chain host
# builds the path itself and never reads module.json's "dsp" field:
#
#   sound_generator  ->  modules/sound_generators/<id>/dsp.so
#   midi_fx          ->  modules/midi_fx/<id>/dsp.so
#   audio_fx         ->  modules/audio_fx/<id>/<id>.so      <-- this one
#
# No -lpthread: this module has no worker thread.

set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE="${MVB_BUILD_IMAGE:-mvb-builder}"
CROSS="${CROSS_PREFIX:-aarch64-linux-gnu-}"

docker build -t "$IMAGE" -f scripts/Dockerfile .

docker run --rm -v "$PWD":/w -w /w "$IMAGE" bash -lc '
  set -euo pipefail
  mkdir -p build/pack
  '"$CROSS"'gcc -Ofast -shared -fPIC \
      -march=armv8-a -mtune=cortex-a72 \
      -fomit-frame-pointer -fno-stack-protector \
      -DNDEBUG \
      -I vendor/schwung -I src \
      -o build/pack/moonivibe.so \
      src/schwung_moonivibe.c src/moonivibe.c \
      -lm
  '"$CROSS"'strip --strip-unneeded build/pack/moonivibe.so
  cp module.json build/pack/
  echo
  echo "exported entry points:"
  '"$CROSS"'nm -D build/pack/moonivibe.so | grep -E "move_audio_fx_init" || echo "  WARNING: no entry point found"
'

echo "built build/pack/"
ls -la build/pack/

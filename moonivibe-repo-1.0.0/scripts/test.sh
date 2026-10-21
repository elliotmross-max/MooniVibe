#!/usr/bin/env bash
# test.sh -- build and run the off-device contract test.
#
# Run it as:  bash scripts/test.sh
#
# Needs only a C compiler and libm. No Docker, no Move, no Schwung checkout.

set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build

CC="${CC:-cc}"

echo "compiling..."
"$CC" -O2 -Wall -Wextra -std=c99 -D_GNU_SOURCE -I src \
    -o build/moonivibe_test \
    src/moonivibe.c src/moonivibe_test.c \
    -lm

echo "compiled clean. running:"
echo
./build/moonivibe_test

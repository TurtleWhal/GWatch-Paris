#!/bin/sh
# Fail-fast: if configure or compile fails, don't fall through to running the
# last-good binary — a stale ./build/gwatch_sim silently masks build breakage
# (this is how the sim drifted weeks behind the firmware once already).
set -e

cd "$(dirname "$0")/simulator"
cmake -B build
cmake --build build -j
./build/gwatch_sim

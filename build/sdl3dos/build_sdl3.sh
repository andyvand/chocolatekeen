#!/bin/sh
# Build Chocolate Keen against SDL3. Requires the SDL3 development package
# discoverable via pkg-config (the `sdl3` module). Pass extra make args through,
# e.g. ./build_sdl3.sh -j4

cp -r ../../data/GAMEDATA GAMEDATA
mkdir -p obj

make "$@"

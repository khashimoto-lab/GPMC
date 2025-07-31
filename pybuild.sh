#!/bin/sh

mkdir -p build
mkdir -p bin
cd build

cmake -DCMAKE_BUILD_TYPE=Release -DGPMC_USE_STATIC_LIBS=on -DGPMC_STATIC_BUILD=on -DGPMC_BUILD_PYTHON=on .. \
&& make -j$(nproc)

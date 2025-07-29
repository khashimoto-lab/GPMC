#!/bin/sh

mkdir -p build
mkdir -p bin
cd build

if [ $1 = "d" ]; then
  echo "Create Makefile for Debug..."
  cmake -DCMAKE_BUILD_TYPE=Debug -DGPMC_USE_STATIC_LIBS=off -DGPMC_STATIC_BUILD=off .. \
  && make -j$(nproc)\
  && cp -p gpmc ../bin/ 

elif [ $1 = "r" ]; then
  echo "Create Makefile for Release..."
  cmake -DCMAKE_BUILD_TYPE=Release -DGPMC_USE_STATIC_LIBS=off -DGPMC_STATIC_BUILD=off .. \
  && make -j$(nproc)\
  && cp -p gpmc ../bin/ 

elif [ $1 = "rs" ]; then
  echo "Create Makefile for Release Static..."
  cmake -DCMAKE_BUILD_TYPE=Release -DGPMC_USE_STATIC_LIBS=on -DGPMC_STATIC_BUILD=on .. \
  && make -j$(nproc)\
  && cp -p gpmc ../bin/
   
fi

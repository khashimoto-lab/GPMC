#!/bin/sh

mkdir -p build
mkdir -p bin
cd build

if [ $1 = "d" ]; then
  echo "Create Makefile for Debug..."
  cmake -DCMAKE_BUILD_TYPE=Debug .. \
  && make \
  && cp -p gpmc ../bin/ 

elif [ $1 = "r" ]; then
  echo "Create Makefile for Release..."
  cmake -DCMAKE_BUILD_TYPE=Release .. \
  && make \
  && cp -p gpmc ../bin/ 

elif [ $1 = "clean" ]; then
  make clean

fi


# GPMC-mc2020 Installation

## Requirement
- g++ 4.7 or later
- cmake and make
- GMP bignum package
- zlib

## Installation
```
$ mkdir build
$ cd build
$ cmake .. 
$ make install
```
The binary executable file *gpmc* is put at the top directory where 
CMakeLists.txt in put.

I have only tested the installation on Ubuntu. 
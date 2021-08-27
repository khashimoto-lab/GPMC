#!/bin/sh

file=$1

grep -e "^cv" $file | sed -e "s/^cv/c p show/" | sed -e "s/\([0-9]*\):A([0-9,]*)/\1/g" | sed -e "s/$/ 0/" >> $file

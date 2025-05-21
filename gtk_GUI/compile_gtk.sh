#! /bin/bash

## maybe do Makefile?

if [[ $# < 1 ]]; then
  echo "No arguments given!";
  exit;
fi

FILENAME=$1;

pkg-config --cflags gtk4;
pkg-config --libs gtk4;

cc `pkg-config --cflags gtk4` $FILENAME.c -o $FILENAME `pkg-config --libs gtk4`;


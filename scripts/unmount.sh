#!/bin/bash

# check if directory was supplied
if [ $# -eq 0 ]; then
    echo "provide directory you wish to unmount"
    exit 1
fi
if [ $# -ne 1 ]; then
  echo "Too many arguments"
fi

# dismount directory
fusermount -u "$1"
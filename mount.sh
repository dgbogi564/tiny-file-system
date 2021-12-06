#!/bin/bash

# check if directory was supplied
if [ $# -eq 0 ]; then
    echo "provide a directory path you wish to mount"
    exit 1
fi
if [ $# -ne 1 ]; then
  echo "Too many arguments"
fi

# create mountdir if it doesn't exist
echo "\"$1\" doesn't exist, creating directory..."
mkdir -p "$1"

# compile and run tiny file system
cd src || exit
make clean
make
./tfs -s "$1"

# check if tiny file system was mounted successfully
findmnt "$1"
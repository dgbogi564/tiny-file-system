#!/bin/bash

cd src || exit
make clean
make
valgrind --leak-check=full --leak-resolution=med --track-origins=yes --vgdb=no -s \
./tfs -f -d -s "/tmp/6098EC24/mountdir"
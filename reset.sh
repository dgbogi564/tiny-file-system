#!/bin/bash

bash tmp_unmount.sh
cd src || exit
make clean
cd ..
mkdir -p "/tmp/6098EC24/mountdir"
#!/bin/bash

echo "Mounting..."
bash scripts/mount.sh "/tmp/6098EC24/mountdir"

cd benchmark || echo "Failed to change directories" && exit
make clean
make
echo "Running benchmarks..."
echo "Running bitmap_check..."
./bitmap_check
echo "Running simple_test..."
./simple_test
echo "Testing mount commands..."
cd "/tmp/6098EC24/mountdir" || echo "Failed to change directories" && exit
echo " Dismounting..."
bash scripts/unmount.sh "/tmp/6098EC24/mountdir"
rm -rfv "/tmp/6098EC24"
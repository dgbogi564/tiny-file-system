# Tiny File System using FUSE Library

### Mounting
```bash
# To make the tiny file system and mount it to "/tmp/mountdir" 
bash scripts/mount.sh "/tmp/mountdir"

# To unmount and delete "/tmp/mountdir"
bash scripts/unmount.sh "/tmp/mountdir"
```

### Benchmarks
Make sure to change the benchmark file's test directory to the folder you mounted to. 
```c
#define TESTDIR "/tmp/mountdir"
```
#!/bin/sh
echo "=== which tools ==="
which make 2>&1 || echo "make: NOT FOUND"
which mips64r5900el-ps2-elf-gcc 2>&1 || echo "gcc: NOT FOUND"
which gcc 2>&1 || echo "host gcc: NOT FOUND"
echo "=== os release ==="
cat /etc/os-release 2>&1
echo "=== apk / apt available? ==="
which apk 2>&1
which apt 2>&1
echo "=== env ==="
env | grep -i ps2

#!/bin/sh
cd ../benchmarks
make
./larson/larson 10 7 8 10000 1000 1 4 4
LD_PRELOAD=../src/libhoard.so ../benchmarks/larson/larson 10 7 8 10000 1000 1 4 4
cd ../src/test
make
LD_PRELOAD=../libhoard.so ./mtest

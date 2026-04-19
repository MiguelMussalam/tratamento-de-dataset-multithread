#!/bin/bash
mkdir -p build
g++ src/main.cpp src/dataset.cpp -Iinclude -o build/main
./build/main
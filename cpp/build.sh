#!/bin/sh
# Builds Changeling Studio (C++) on Linux: needs cmake, a C++17 compiler, libsdl2-dev, ffmpeg (videos, the webcam),
# and ONNX Runtime's libonnxruntime.so next to the program (or on the library path).
set -e
cd "$(dirname "$0")"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
echo "Built: build/ChangelingStudio"

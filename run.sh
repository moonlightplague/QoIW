#!/usr/bin/env bash
set -euo pipefail

WORKERS=128
INPUT="data/Hurricane-Dataset-for-Compressors/CLOUDf48.bin.f32"
COMPRESS_OUTPUT="output/Hurricane-Dataset-for-Compressors/CLOUDf48.bin.f32.qoiw"
DECOMPRESS_OUTPUT="output/Hurricane-Dataset-for-Compressors/CLOUDf48.bin.f32.out"

#compress
mkdir -p "$(dirname "$COMPRESS_OUTPUT")"
./build/qoiw compress -f \
    --shape 500,500,100 -i "$INPUT" \
    -q 'x^2' -m REL 1e-3 \
    -z "$COMPRESS_OUTPUT" \
    -t $WORKERS -a

#decompress
mkdir -p "$(dirname "$DECOMPRESS_OUTPUT")"
./build/qoiw decompress \
    -z "$COMPRESS_OUTPUT" \
    -o "$DECOMPRESS_OUTPUT"
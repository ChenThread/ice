#!/bin/sh
# Friendly script to take video files and output an ICE file.
ffmpeg -v 0 -i "$1" -s 160x50 -r 20 -f rawvideo -pix_fmt rgb24 - | ./ice -i - -o "$2"

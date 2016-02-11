#!/bin/sh
# Video tester - requires mpv (alternatively you can use ffplay)
ffmpeg -v 0 -i "$1" -s 160x50 -r 20 -f rawvideo -pix_fmt rgb24 - 2>/dev/null | \
./ice -i /dev/stdin -o /dev/null -d /dev/stdout | \
ffmpeg -v 0 -f rawvideo -pix_fmt rgb24 -s 160x50 -r 20 -i - -vn -i "$1" -map 0:0 -map "1$2" -f avi -acodec mp3 -vcodec rawvideo -r 20 -s 160x50 -pix_fmt bgr24 -vf setdar=1.78 - 2>/dev/null | \
mpv -

#ffplay - 2>/dev/null


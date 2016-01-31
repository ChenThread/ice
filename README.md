# Intelligent Cirno Experience

ICE is a video codec for OpenComputers created by ChenThread in a few days for BetterThanMinecon 2016.

Yes, it is slow. I recommend you compile with at least -O2.

* **ice.c** - The encoder. Use --help for help. It doesn't let you do much right now - just encode 160x50 RGB24 data.
* **ice-player.lua** - The ICE-encoded video player for OpenComputers. Will play a tape if one is inserted.
* **ice-wrapper.sh** - A friendly wrapper which takes in a video file and outputs an ICE-encoded video file. Does not keep aspect ratio. Requires ffmpeg.
* **Makefile** - It holds the bits together.
* **README.md** - It explains the bits which are being held together.

## Compiling

    $ git submodule init
    $ git submodule update
    $ make

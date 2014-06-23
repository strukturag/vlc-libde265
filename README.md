# vlc-libde265

Plugins for VLC 2.x to support HEVC decoding using libde265. For VLC
versions below 2.1, a modified MKV demuxer is included which has support
for HEVC streams. For VLC versions below 2.2, modified MPEG-TS and MP4
demuxers are included which have support for HEVC streams.


## Building

[![Build Status](https://travis-ci.org/strukturag/vlc-libde265.png?branch=master)](https://travis-ci.org/strukturag/vlc-libde265)

Execute the default `configure` / `make` steps:

    $ ./configure
    $ make

If you fetched vlc-libde265 from GitHub, you will need to generate the
`configure` script first:

    $ ./autogen.sh


## Dependencies

In addition to a compiler, a couple of other packages must be installed
in order to compile the plugin:
- libvlccore-dev
- libde265-dev (>= 0.7)
- libebml-dev
- libmatroska-dev
- libdvbpsi-dev

These should be available from the package management on any recent
version of Debian / Ubuntu.


## Patches

See the `patches` folder for changes applied to the VLC source code of
the demuxers to make them compile against older versions of VLC.


## Settings

In the advanced settings of VLC, a couple of properties can be configured
for the libde265 plugins (below "Demuxers" and "Video codecs"):
- Framerate for raw bitstream demuxer (25 fps is assumed by default)
- Number of threads to use for decoding ("auto" by default)
- Whether the deblocking filter should be disabled (enabled by default)
- Whether the sample-adaptive-offset filter should be disabled (enabled by default)


## Packages

Binary packages for Ubuntu are available on Launchpad:
https://launchpad.net/~strukturag/+archive/libde265


Copyright (c) 2014 struktur AG

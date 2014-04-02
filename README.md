# vlc-libde265

Plugin for VLC 2.x to support HEVC decoding using libde265. For VLC
versions below 2.1, a modified MKV demuxer is included which has support
for HEVC streams.


## Building

[![Build Status](https://travis-ci.org/strukturag/vlc-libde265.png?branch=master)](https://travis-ci.org/strukturag/vlc-libde265)

Execute the default `configure` / `make` steps:

    $ ./configure
    $ make

If you fetched vlc-libde265 from GitHub, you will need to generate the
`configure` script first:

    $ ./autogen.sh


## Patches

See the `patches` folder for changes applied to the VLC source code of
the MKV demuxer to make it compile against VLC 2.0 in this plugin.


Copyright (c) 2014 struktur AG

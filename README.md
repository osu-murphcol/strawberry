:strawberry: Strawberry Music Player [![Build Status](https://travis-ci.com/osu-murphcol/strawberry.svg?branch=master)](https://travis-ci.com/osu-murphcol/strawberry)
[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=FRJUYV5QP6HW8)
=======================

Strawberry is a music player and music collection organizer. It is a fork of Clementine released in 2018 aimed at music collectors, audio enthusiasts and audiophiles. The name is inspired by the band Strawbs. It's based on a heavily modified version of Clementine created in 2012-2013. It's written in C++ and Qt 5.

  * Website: https://www.strawbs.org/
  * Github: https://github.com/jonaski/strawberry
  * Buildbot: http://buildbot.strawbs.net/
  * Latest builds: https://builds.strawbs.org/

### :heavy_check_mark: Features:

  * Play and organize music
  * Supports WAV, FLAC, WavPack, DSF, DSDIFF, Ogg Vorbis, Speex, MPC, TrueAudio, AIFF, MP4, MP3, ASF and Monkey's Audio.
  * Audio CD playback
  * Native desktop notifications
  * Playlists in multiple formats
  * Advanced audio output and device configuration for bit-perfect playback on Linux
  * Edit tags on music files
  * Fetch tags from MusicBrainz
  * Album cover art from Last.fm, Musicbrainz, Discogs, Deezer and Tidal
  * Song lyrics from AudD and ChartLyrics
  * Support for multiple backends
  * Audio analyzer
  * Audio equalizer
  * Transfer music to iPod, iPhone, MTP or mass-storage USB player
  * Streaming support for Tidal
  * Scrobbler with support for Last.fm, Libre.fm and ListenBrainz

It has so far been tested to work on Linux, OpenBSD, macOS and Windows.

### :heavy_exclamation_mark: Requirements

To build Strawberry from source you need the following installed on your system with the additional development packages/headers:

* [GLib, GIO and GObject](https://developer.gnome.org/glib/)
* [POSIX thread (pthread) libraries](http://www.yolinux.com/TUTORIALS/LinuxTutorialPosixThreads.html)
* [CMake and Make tools](https://cmake.org/)
* [GCC](https://gcc.gnu.org/) or [clang](https://clang.llvm.org/) compiler
* [Protobuf library and compiler](https://developers.google.com/protocol-buffers/)
* [Boost development headers](https://www.boost.org/)
* [Qt 5 with components Core, Gui, Widgets, Concurrent, Network and Sql](https://www.qt.io/)
* [Qt 5 components X11Extras and DBus for Linux/BSD, MacExtras for macOS and WinExtras for Windows](https://www.qt.io/)
* [SQLite3](https://www.sqlite.org)
* [TagLib 1.11.1 or higher](http://taglib.org/)
* [Chromaprint library](https://acoustid.org/chromaprint)
* [ALSA library (linux)](https://www.alsa-project.org/)
* [DBus (linux)](https://www.freedesktop.org/wiki/Software/dbus/)
* [PulseAudio (linux optional)](https://www.freedesktop.org/wiki/Software/PulseAudio/?)
* [GStreamer](https://gstreamer.freedesktop.org/), [Xine](https://www.xine-project.org), [VLC](https://www.videolan.org) or [Phonon](https://techbase.kde.org/Phonon)
* [GnuTLS](https://www.gnutls.org/)

Optional dependencies:

* Audio CD: [libcdio](https://www.gnu.org/software/libcdio/)
* MTP devices: [libmtp](http://libmtp.sourceforge.net/)
* iPod Classic devices: [libgpod](http://www.gtkpod.org/libgpod/)
* iPhone, iPod Touch, iPad and Apple TV devices: [libimobiledevice, libplist and libusbmuxd](https://www.libimobiledevice.org/)
* Moodbar: [fftw3](http://www.fftw.org/)

Either GStreamer, Xine, VLC or Phonon engine is required, but only GStreamer is fully implemented so far.
You should also install the gstreamer plugins base and good, and optionally bad and ugly.

### :wrench:	Compiling from source

### Get the code:

    git clone https://github.com/jonaski/strawberry

### Compile and install:

    mkdir strawberry-build
    cd strawberry-build
    cmake ../strawberry
    make -j8
    sudo make install

    (dont change to the source directory, if you created the build directory inside the source directory type: cmake .. instead).

### :penguin:	Packaging status

[![Packaging status](https://repology.org/badge/vertical-allrepos/strawberry.svg)](https://repology.org/metapackage/strawberry/versions)

### :computer:	Screenshot


![Browse](https://www.strawbs.org/pictures/screenshot-002-large.png)

### :moneybag: Donate

[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=FRJUYV5QP6HW8)

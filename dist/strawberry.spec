Name:           strawberry
Version:        0.1.1
Release:        1.fc13
Summary:        A audio player and music collection organiser

Group:          Applications/Multimedia
License:        GPLv3
URL:            http://www.strawbs.org/
Source0:        %{name}-0.1.1.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  desktop-file-utils liblastfm-devel taglib-devel gettext
BuildRequires:  qt5-devel boost-devel gcc-c++ glew-devel libgpod-devel
BuildRequires:  cmake gstreamer-devel gstreamer-plugins-base-devel
BuildRequires:  libimobiledevice-devel libplist-devel usbmuxd-devel
BuildRequires:  libmtp-devel protobuf-devel protobuf-compiler libcdio-devel
BuildRequires:  qjson-devel qca2-devel fftw-devel sparsehash-devel
BuildRequires:  libchromaprint-devel

Requires:       libgpod protobuf-lite libcdio qjson qca-ossl sqlite

# GStreamer codec dependencies
Requires:       gstreamer-plugins-ugly

%ifarch x86_64
Requires:       gstreamer1.0(decoder-audio/x-vorbis)()(64bit)
Requires:       gstreamer1.0(decoder-audio/x-flac)()(64bit)
Requires:       gstreamer1.0(decoder-audio/x-speex)()(64bit)
Requires:       gstreamer1.0(decoder-audio/x-wav)()(64bit)
%else
Requires:       gstreamer1.0(decoder-audio/x-vorbis)
Requires:       gstreamer1.0(decoder-audio/x-flac)
Requires:       gstreamer1.0(decoder-audio/x-speex)
Requires:       gstreamer1.0(decoder-audio/x-wav)
%endif

%description
Strawberry is a modern audio player and music collection organiser.
It is a fork of Clementine. The name is inspired by the band Strawbs.

Features include:

  * Organize and play your music collection
  * Edit tags on your music
  * Download album cover art from Last.fm, musicbrainz, Discogs and Amazon
  * Native desktop notifications
  * Import and export playlists in multiple formats
  * Copy music to your iPod, iPhone, MTP or mass-storage USB player
  * Support for multiple backends

%prep
%setup -q -n %{name}-0.1.1


%build
cd bin
%{cmake} .. -DUSE_INSTALL_PREFIX=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make %{?_smp_mflags}

%install
cd bin
make install DESTDIR=$RPM_BUILD_ROOT
rm -f $RPM_BUILD_ROOT/usr/share/icons/ubuntu-mono-{dark,light}/apps/24/strawberry-panel*.png

%clean
cd bin
make clean


%files
%defattr(-,root,root,-)
%doc
%{_bindir}/strawberry
%{_bindir}/strawberry-tagreader
%{_datadir}/applications/strawberry.desktop
%{_datadir}/strawberry/projectm-presets
%{_datadir}/kde4/services/strawberry-itms.protocol
%{_datadir}/kde4/services/strawberry-itpc.protocol
%{_datadir}/kde4/services/strawberry-feed.protocol
%{_datadir}/kde4/services/strawberry-zune.protocol
%{_datadir}/icons/hicolor/64x64/apps/strawberry.png
%{_datadir}/icons/hicolor/128x128/apps/strawberry.png
%{_datadir}/icons/hicolor/scalable/apps/strawberry.svg

%changelog
* ma. feb. 26 2018 0.1.1
- Version 0.1.1
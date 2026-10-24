FROM debian:bullseye
ENV DEBIAN_FRONTEND=noninteractive

# The TrimUI firmware (CrossMix/JukaMix) is glibc 2.33. Building on bullseye
# (glibc 2.31, GCC 10) keeps the binary compatible; Ubuntu 22.04 would ask for
# GLIBC_2.34 and fail at start.
RUN printf 'deb http://snapshot.debian.org/archive/debian/20260824T000000Z bullseye main\ndeb http://snapshot.debian.org/archive/debian-security/20260824T000000Z bullseye-security main\n' > /etc/apt/sources.list \
 && dpkg --add-architecture arm64 \
 && apt-get -o Acquire::Check-Valid-Until=false update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      g++-aarch64-linux-gnu \
      cmake \
      ninja-build \
      pkg-config \
      ca-certificates \
      curl \
      libpng-dev:arm64 \
      zlib1g-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*

# The device ships SDL2 2.30.8 with its Mali video driver. We only need a
# matching SDL2 to link against (the runtime uses the system one), so build it
# for aarch64 without video/audio backends.
ARG SDL2_VERSION=2.30.8
RUN curl -L --fail -o /tmp/SDL2.tar.gz "https://www.libsdl.org/release/SDL2-${SDL2_VERSION}.tar.gz" \
 && mkdir -p /tmp/sdl2 \
 && tar -xzf /tmp/SDL2.tar.gz -C /tmp/sdl2 --strip-components=1 \
 && cd /tmp/sdl2 \
 && ./configure --host=aarch64-linux-gnu --prefix=/opt/sdl2 \
      --disable-video-x11 --disable-video-wayland --disable-video-kmsdrm \
      --disable-video-mir --disable-video-rpi --disable-video-vivante \
      --disable-video-cocoa --disable-video-metal --disable-video-opengl \
      --disable-video-vulkan --disable-pulseaudio --disable-jack --disable-pipewire \
      --disable-ime --disable-libudev --disable-hidapi --disable-libsamplerate \
      --disable-directfb --disable-fcitx --disable-ibus \
 && make -j"$(nproc)" \
 && make install \
 && rm -rf /tmp/sdl2 /tmp/SDL2.tar.gz

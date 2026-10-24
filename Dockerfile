FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

RUN dpkg --add-architecture arm64 && \
    sed -i 's/deb http/deb [arch=amd64] http/g' /etc/apt/sources.list && \
    echo 'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy main restricted universe multiverse' >> /etc/apt/sources.list && \
    echo 'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-updates main restricted universe multiverse' >> /etc/apt/sources.list && \
    echo 'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports/ jammy-security main restricted universe multiverse' >> /etc/apt/sources.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        gcc-aarch64-linux-gnu \
        g++-aarch64-linux-gnu \
        cmake \
        ninja-build \
        libsdl2-dev:arm64 \
        libpng-dev:arm64 \
        zlib1g-dev:arm64 \
        pkg-config && \
    rm -rf /var/lib/apt/lists/*

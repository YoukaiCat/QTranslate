# Reproducible Debian 13 (trixie) build environment for the Linux baseline.
# trixie ships Qt 6.8.2, which satisfies the project's Qt 6.5+ minimum.
#
#   docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .
#   docker run --rm -v "$PWD:/src:ro" -w /src qtranslate-trixie bash -c \
#       'cmake -S /src -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
#        && cmake --build /tmp/build --parallel \
#        && xvfb-run -a ctest --test-dir /tmp/build --output-on-failure'
#
# Task 06 adds the packaging tooling (dpkg-dev, desktop-file-utils, AppImage).
FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        qt6-base-dev \
        qt6-tools-dev \
        qt6-tools-dev-tools \
        libx11-dev \
        libxtst-dev \
        libsecret-1-dev \
        # xvfb only Recommends xauth, but xvfb-run requires it.
        xvfb \
        xauth \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

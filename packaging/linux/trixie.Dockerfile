# Reproducible Debian 13 (trixie) build environment for the Linux baseline.
# trixie ships Qt 6.8.2, which satisfies the project's Qt 6.5+ minimum.
#
#   docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .
#   docker run --rm -v "$PWD:/src:ro" -w /src qtranslate-trixie bash -c \
#       'cmake -S /src -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
#        && cmake --build /tmp/build --parallel \
#        && xvfb-run -a ctest --test-dir /tmp/build --output-on-failure'
#
# Stage 07-packaging-and-ci adds the packaging tooling as its tasks need it:
# this task adds desktop-file-utils; later ones add dpkg-dev, patchelf, file,
# curl, and the AppImage tools.
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
        # dbus-daemon is the private session bus for tst_hotkeymanager_wayland;
        # in trixie the daemon is the separate dbus-daemon package.
        dbus \
        # xvfb only Recommends xauth, but xvfb-run requires it.
        xvfb \
        xauth \
        # desktop-file-validate is the gate for packaging/linux/qtranslate.desktop.
        desktop-file-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

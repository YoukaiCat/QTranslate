# Reproducible Debian 13 (trixie) build environment for the Linux baseline.
# trixie ships Qt 6.8.2, which satisfies the project's Qt 6.5+ minimum.
#
#   docker build -f packaging/linux/trixie.Dockerfile -t qtranslate-trixie .
#   docker run --rm -v "$PWD:/src:ro" -w /src qtranslate-trixie bash -c \
#       'cmake -S /src -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
#        && cmake --build /tmp/build --parallel \
#        && xvfb-run -a ctest --test-dir /tmp/build --output-on-failure'
#
# The apt package list lives in packaging/linux/trixie-packages.txt and is
# shared with the Linux CI job. The AppImage tools themselves are downloaded
# by package.sh, not installed in the image.
FROM debian:trixie

ENV DEBIAN_FRONTEND=noninteractive

COPY packaging/linux/trixie-packages.txt /tmp/trixie-packages.txt

RUN apt-get update \
    && grep -vE '^[[:space:]]*(#|$)' /tmp/trixie-packages.txt \
        | xargs apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

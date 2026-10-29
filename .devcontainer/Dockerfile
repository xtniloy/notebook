# Development image: Ubuntu 26.04 (the minimum platform, ADR 0001) with every
# build, test and analysis dependency from scripts/setup-dev.sh.
#
# Used by .devcontainer/devcontainer.json and scripts/dev-container.sh. Build
# context is the repository root (see .dockerignore).

FROM ubuntu:26.04

# --ci skips gdb, valgrind, heaptrack and perf; pass SETUP_FLAGS="" to get them.
ARG SETUP_FLAGS=--ci

COPY scripts/setup-dev.sh /tmp/setup-dev.sh
RUN /tmp/setup-dev.sh ${SETUP_FLAGS} \
    && rm -rf /var/lib/apt/lists/* /tmp/setup-dev.sh

# Qt warns and falls back to C.UTF-8 under the default "C" locale.
ENV LANG=C.UTF-8

# The source tree is mounted here, with build/ on a named volume so container
# builds never mix with native ones (CMakeCache.txt pins absolute paths). A new
# volume copies this directory's mode, so any host UID can write to it.
RUN install -d -m 1777 /workspaces/notebook/build
WORKDIR /workspaces/notebook

FROM fedora:41
ENV DEBIAN_FRONTEND=noninteractive

RUN dnf update -y && \
    dnf install -y --setopt=install_weak_deps=False \
        gcc gcc-c++ make cmake python3 python3-pip \
        boost-devel openssl-devel libcurl-devel libicu-devel \
        glib2-devel dbus-devel \
        fontconfig freetype mesa-libGL-devel mesa-libGLU-devel \
        libxkbcommon-x11 xcb-util-wm xcb-util-keysyms xcb-util-renderutil xcb-util-cursor \
        ragel ninja-build zlib-devel git pkgconf-pkg-config wget ca-certificates fuse file which && \
    dnf clean all

RUN python3 -m venv /tmp/aqt-venv && \
    /tmp/aqt-venv/bin/python -m pip install --no-cache-dir --upgrade pip setuptools wheel && \
    /tmp/aqt-venv/bin/python -m pip install --no-cache-dir "git+https://github.com/miurahr/aqtinstall.git@v3.2.1" && \
    ARCH=$(uname -m) && \
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then QT_AQT_ARCH=linux_gcc_arm64; QT_DIR=gcc_arm64; else QT_AQT_ARCH=linux_gcc_64; QT_DIR=gcc_64; fi && \
    /tmp/aqt-venv/bin/python -m aqt install-qt linux desktop 6.10.1 ${QT_AQT_ARCH} -O /opt/Qt && \
    /tmp/aqt-venv/bin/python -m aqt install-qt linux desktop 6.10.1 ${QT_AQT_ARCH} -O /opt/Qt \
        --modules qtimageformats qtserialport qt5compat && \
    ln -s "/opt/Qt/6.10.1/${QT_DIR}" /opt/Qt/6.10.1/current

ENV QT_ROOT=/opt/Qt/6.10.1/current
ENV CMAKE_PREFIX_PATH=/opt/Qt/6.10.1/current/lib/cmake
ENV PATH=/opt/Qt/6.10.1/current/bin:$PATH
ENV CC=gcc
ENV CXX=g++

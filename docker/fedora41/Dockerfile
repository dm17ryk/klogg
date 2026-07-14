FROM fedora:41
ENV DEBIAN_FRONTEND=noninteractive

RUN dnf update -y && \
    dnf install -y --setopt=install_weak_deps=False \
        gcc gcc-c++ make cmake python3 python3-pip rpm-build \
        boost-devel openssl-devel libcurl-devel libicu-devel \
        glib2-devel dbus-devel \
        fontconfig freetype mesa-libGL-devel mesa-libGLU-devel \
        libxkbcommon-x11 xcb-util-wm xcb-util-keysyms xcb-util-renderutil xcb-util-cursor \
        ragel ninja-build zlib-devel git pkgconf-pkg-config wget ca-certificates fuse file which && \
    dnf clean all

RUN python3 -m venv /tmp/aqt-venv && \
    /tmp/aqt-venv/bin/python -m pip install --no-cache-dir --upgrade pip setuptools wheel && \
    /tmp/aqt-venv/bin/python -m pip install --no-cache-dir "aqtinstall==3.3.0" && \
    printf '%s\n' \
        '[aqt]' \
        'baseurl: https://download.qt.io' \
        '' \
        '[requests]' \
        'connection_timeout: 10' \
        'response_timeout: 60' \
        'max_retries_on_connection_error: 5' \
        'retry_backoff: 1' \
        'max_retries_on_checksum_error: 5' \
        'max_retries_to_retrieve_hash: 5' \
        '' \
        '[mirrors]' \
        'blacklist:' \
        '    https://qt.mirror.constant.com' \
        'fallbacks:' \
        '    https://master.qt.io' \
        '    https://mirrors.ocf.berkeley.edu/qt' \
        '    https://ftp.jaist.ac.jp/pub/qtproject' \
        > /tmp/aqtinstall.ini && \
    ARCH=$(uname -m) && \
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then QT_AQT_OS=linux_arm64; QT_AQT_ARCH=linux_gcc_arm64; QT_DIR=gcc_arm64; else QT_AQT_OS=linux; QT_AQT_ARCH=linux_gcc_64; QT_DIR=gcc_64; fi && \
    AQT_CONFIG=/tmp/aqtinstall.ini /tmp/aqt-venv/bin/python -m aqt install-qt ${QT_AQT_OS} desktop 6.10.1 ${QT_AQT_ARCH} -O /opt/Qt && \
    AQT_CONFIG=/tmp/aqtinstall.ini /tmp/aqt-venv/bin/python -m aqt install-qt ${QT_AQT_OS} desktop 6.10.1 ${QT_AQT_ARCH} -O /opt/Qt \
        --modules qtimageformats qtserialport qt5compat && \
    QT_SQL_CONFIG="/opt/Qt/6.10.1/${QT_DIR}/lib/cmake/Qt6Sql/Qt6SqlConfig.cmake" && \
    if [ ! -f "$QT_SQL_CONFIG" ]; then \
        echo "Qt6Sql CMake package is missing: $QT_SQL_CONFIG"; \
        exit 1; \
    fi && \
    echo "Qt6Sql CMake package verified: $QT_SQL_CONFIG" && \
    ln -s "/opt/Qt/6.10.1/${QT_DIR}" /opt/Qt/6.10.1/current

ENV QT_ROOT=/opt/Qt/6.10.1/current
ENV CMAKE_PREFIX_PATH=/opt/Qt/6.10.1/current/lib/cmake
ENV PATH=/opt/Qt/6.10.1/current/bin:$PATH
ENV CC=gcc
ENV CXX=g++

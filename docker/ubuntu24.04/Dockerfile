FROM ubuntu:noble
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y \
 && apt-get install --no-install-recommends \
        build-essential python3 python-is-python3 python3-pip python3-venv \
        libboost-dev libicu-dev libssl-dev libcurl4-openssl-dev \
        libglib2.0-dev \
        libdbus-1-dev \
        libfontconfig1 libfreetype6 libgl1-mesa-dev libglu1-mesa-dev \
        libxkbcommon-x11-0 libxcb-icccm4 libxcb-keysyms1 libxcb-render-util0 \
        libxcb-shape0 libxcb-sync1 libxcb-xfixes0 libxcb-xinerama0 libxcb-randr0 \
        ragel ninja-build zlib1g-dev git pkg-config wget ca-certificates fuse file -y \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

ENV PIP_BREAK_SYSTEM_PACKAGES=1

RUN python3 -m venv /tmp/aqt-venv && \
    /tmp/aqt-venv/bin/python -m pip install --no-cache-dir --upgrade pip setuptools wheel && \
    /tmp/aqt-venv/bin/python -m pip install --no-cache-dir "git+https://github.com/miurahr/aqtinstall.git@v3.2.1" && \
    ARCH=$(uname -m) && \
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then QT_AQT_OS=linux_arm64; QT_AQT_ARCH=linux_gcc_arm64; QT_DIR=gcc_arm64; CMAKE_ARCH=aarch64; else QT_AQT_OS=linux; QT_AQT_ARCH=linux_gcc_64; QT_DIR=gcc_64; CMAKE_ARCH=x86_64; fi && \
    /tmp/aqt-venv/bin/python -m aqt install-qt ${QT_AQT_OS} desktop 6.10.1 ${QT_AQT_ARCH} -O /opt/Qt && \
    /tmp/aqt-venv/bin/python -m aqt install-qt ${QT_AQT_OS} desktop 6.10.1 ${QT_AQT_ARCH} -O /opt/Qt \
        --modules qtimageformats qtserialport qt5compat && \
    ln -s "/opt/Qt/6.10.1/${QT_DIR}" /opt/Qt/6.10.1/current && \
    wget "https://github.com/Kitware/CMake/releases/download/v3.28.6/cmake-3.28.6-linux-${CMAKE_ARCH}.sh" && \
    chmod 755 "cmake-3.28.6-linux-${CMAKE_ARCH}.sh" && \
    "./cmake-3.28.6-linux-${CMAKE_ARCH}.sh" --prefix=/opt/ --exclude-subdir --skip-license && \
    rm "cmake-3.28.6-linux-${CMAKE_ARCH}.sh"

ENV QT_ROOT=/opt/Qt/6.10.1/current
ENV CMAKE_PREFIX_PATH=/opt/Qt/6.10.1/current/lib/cmake
ENV PATH=/opt/bin:/opt/Qt/6.10.1/current/bin:$PATH

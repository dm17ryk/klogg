FROM ubuntu:noble
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y \
 && apt-get install --no-install-recommends \
        build-essential python3 python-is-python3 python3-pip \
        libboost-dev libicu-dev libssl-dev libcurl4-openssl-dev \
        libfontconfig1 libfreetype6 libgl1-mesa-dev libglu1-mesa-dev \
        libxkbcommon-x11-0 libxcb-icccm4 libxcb-keysyms1 libxcb-render-util0 \
        libxcb-shape0 libxcb-sync1 libxcb-xfixes0 libxcb-xinerama0 libxcb-randr0 \
        ragel ninja-build zlib1g-dev git pkg-config wget ca-certificates fuse file -y \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

ENV PIP_BREAK_SYSTEM_PACKAGES=1

RUN python3 -m pip install --no-cache-dir aqtinstall==3.2.1 && \
    python3 -m aqt install-qt linux desktop 6.10.1 linux_gcc_64 -O /opt/Qt \
        --modules qtimageformats qtserialport qt5compat

RUN wget https://github.com/Kitware/CMake/releases/download/v3.20.2/cmake-3.20.2-linux-x86_64.sh && \
    chmod 755 cmake-3.20.2-linux-x86_64.sh && \
    ./cmake-3.20.2-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-3.20.2-linux-x86_64.sh

ENV QT_ROOT=/opt/Qt/6.10.1/linux_gcc_64
ENV CMAKE_PREFIX_PATH=/opt/Qt/6.10.1/linux_gcc_64/lib/cmake
ENV PATH=/opt/bin:/opt/Qt/6.10.1/linux_gcc_64/bin:$PATH

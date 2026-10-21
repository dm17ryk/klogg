FROM ubuntu:noble
ENV DEBIAN_FRONTEND=noninteractive
ARG QT_VERSION=6.10.1

RUN rm -rf /var/lib/apt/lists/* \
 && for attempt in 1 2 3 4 5; do \
        apt-get -o Acquire::Retries=3 update -y && break; \
        if [ "$attempt" -eq 5 ]; then exit 1; fi; \
        rm -rf /var/lib/apt/lists/*; \
        sleep 5; \
    done \
 && apt-get -o Acquire::Retries=3 install --no-install-recommends \
        build-essential python3 python-is-python3 python3-pip python3-venv \
        libboost-dev libicu-dev libssl-dev libcurl4-openssl-dev \
        libglib2.0-dev \
        libdbus-1-dev \
        libfontconfig1 libfreetype6 libgl1-mesa-dev libglu1-mesa-dev \
        libxkbcommon-x11-0 libxcb-icccm4 libxcb-keysyms1 libxcb-render-util0 \
        libxcb-shape0 libxcb-sync1 libxcb-xfixes0 libxcb-xinerama0 libxcb-randr0 \
        libxcb-cursor0 libsm6 libice6 libwayland-cursor0 \
        libtiff6 libwebp7 libwebpdemux2 libwebpmux3 \
        ragel ninja-build zlib1g-dev git pkg-config wget ca-certificates fuse file -y \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

ENV PIP_BREAK_SYSTEM_PACKAGES=1

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
        '    https://ftp1.nluug.nl' \
        '    https://ftp2.nluug.nl' \
        'fallbacks:' \
        '    https://master.qt.io' \
        '    https://mirrors.ocf.berkeley.edu/qt' \
        '    https://ftp.jaist.ac.jp/pub/qtproject' \
        > /tmp/aqtinstall.ini && \
    ARCH=$(uname -m) && \
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then QT_AQT_OS=linux_arm64; QT_AQT_ARCH=linux_gcc_arm64; QT_DIR=gcc_arm64; CMAKE_ARCH=aarch64; else QT_AQT_OS=linux; QT_AQT_ARCH=linux_gcc_64; QT_DIR=gcc_64; CMAKE_ARCH=x86_64; fi && \
    echo "Installing Qt ${QT_VERSION}: host=${QT_AQT_OS} arch=${QT_AQT_ARCH}" && \
    AQT_CONFIG=/tmp/aqtinstall.ini /tmp/aqt-venv/bin/python -m aqt install-qt ${QT_AQT_OS} desktop ${QT_VERSION} ${QT_AQT_ARCH} -O /opt/Qt \
        --modules qtimageformats qtserialport qt5compat && \
    ln -s "/opt/Qt/${QT_VERSION}/${QT_DIR}" "/opt/Qt/${QT_VERSION}/current" && \
    wget "https://github.com/Kitware/CMake/releases/download/v3.28.6/cmake-3.28.6-linux-${CMAKE_ARCH}.sh" && \
    chmod 755 "cmake-3.28.6-linux-${CMAKE_ARCH}.sh" && \
    "./cmake-3.28.6-linux-${CMAKE_ARCH}.sh" --prefix=/opt/ --exclude-subdir --skip-license && \
    rm "cmake-3.28.6-linux-${CMAKE_ARCH}.sh"

ENV QT_ROOT=/opt/Qt/${QT_VERSION}/current
ENV CMAKE_PREFIX_PATH=/opt/Qt/${QT_VERSION}/current/lib/cmake
ENV PATH=/opt/bin:/opt/Qt/${QT_VERSION}/current/bin:$PATH
RUN qmake -query QT_VERSION && gcc --version && cmake --version

# ARM64 AppImages use this image; x64 AppImages use the separate Jammy image.
# The older x64 Qt TIFF plugin ABI is not part of Noble's DEB build contract.
# Report every missing ARM portable dependency; optional SQL backends are excluded.
RUN case "$(uname -m)" in \
        aarch64|arm64) ;; \
        *) echo "ARM64 portable plugin check: not applicable to this architecture"; exit 0 ;; \
    esac; \
    missing=0; \
    for plugin in "$QT_ROOT"/plugins/platforms/*.so \
                  "$QT_ROOT"/plugins/imageformats/*.so \
                  "$QT_ROOT"/plugins/iconengines/*.so \
                  "$QT_ROOT"/plugins/xcbglintegrations/*.so \
                  "$QT_ROOT"/plugins/sqldrivers/libqsqlite.so; do \
        [ -f "$plugin" ] || continue; \
        echo "Checking Qt plugin dependencies: $plugin"; \
        deps=$(ldd "$plugin") || exit 1; \
        if printf '%s\n' "$deps" | grep 'not found'; then missing=1; fi; \
    done; \
    test "$missing" -eq 0

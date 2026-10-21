ARG BASE_IMAGE=debian:12
FROM ${BASE_IMAGE}
ARG QT_VERSION=6.12.0
ENV DEBIAN_FRONTEND=noninteractive
COPY install-system-deps.sh /tmp/install-system-deps.sh
RUN bash /tmp/install-system-deps.sh

RUN python3 -m venv /tmp/aqt-venv \
 && /tmp/aqt-venv/bin/python -m pip install --no-cache-dir "aqtinstall==3.3.0" \
 && printf '%s\n' '[aqt]' 'baseurl: https://download.qt.io' '' \
      '[requests]' 'connection_timeout: 10' 'response_timeout: 60' \
      'max_retries_on_connection_error: 5' 'retry_backoff: 1' '' \
      '[mirrors]' 'blacklist:' '    https://ftp1.nluug.nl' '    https://ftp2.nluug.nl' \
      'fallbacks:' '    https://master.qt.io' > /tmp/aqtinstall.ini \
 && echo "Installing Qt ${QT_VERSION}: linux x64 SDK" \
 && AQT_CONFIG=/tmp/aqtinstall.ini /tmp/aqt-venv/bin/python -m aqt \
      install-qt linux desktop ${QT_VERSION} linux_gcc_64 -O /opt/Qt \
      --modules qtimageformats qtserialport qt5compat \
      --archives qtbase qtsvg qttools qttranslations \
 && wget -q https://github.com/Kitware/CMake/releases/download/v3.28.6/cmake-3.28.6-linux-x86_64.sh -O /tmp/cmake.sh \
 && sh /tmp/cmake.sh --prefix=/opt --exclude-subdir --skip-license \
 && rm /tmp/cmake.sh

ENV QT_ROOT=/opt/Qt/${QT_VERSION}/gcc_64
ENV CMAKE_PREFIX_PATH=/opt/Qt/${QT_VERSION}/gcc_64
ENV PATH=/opt/bin:/opt/Qt/${QT_VERSION}/gcc_64/bin:$PATH
ENV CC=gcc
ENV CXX=g++
RUN cat /etc/os-release && qmake -query QT_VERSION && gcc --version && cmake --version

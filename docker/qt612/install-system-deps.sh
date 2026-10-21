#!/usr/bin/env bash
set -euo pipefail
source /etc/os-release
echo "Installing native Qt 6.12 build prerequisites: id=$ID version=$VERSION_ID architecture=$(uname -m)"
case "$ID" in
  debian)
    apt-get -o Acquire::Retries=3 update
    apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
      build-essential python3 python3-venv libboost-dev libicu-dev \
      libssl-dev libcurl4-openssl-dev libglib2.0-dev libdbus-1-dev \
      libfontconfig1 libfreetype6 libgl1-mesa-dev libglu1-mesa-dev \
      libxkbcommon-x11-0 libxcb-icccm4 libxcb-keysyms1 libxcb-render-util0 \
      libxcb-shape0 libxcb-sync1 libxcb-xfixes0 libxcb-xinerama0 libxcb-randr0 \
      libxcb-cursor0 libsm6 libice6 libwayland-cursor0 \
      libtiff6 libwebp7 libwebpdemux2 libwebpmux3 \
      ragel ninja-build zlib1g-dev git pkg-config wget ca-certificates file
    apt-get clean
    ;;
  rhel)
    # UBI's public repositories suffice for the Qt-only build. Avoid adding
    # subscription-only repositories or third-party RPMs to the RHEL baseline.
    dnf install -y --setopt=install_weak_deps=False \
      gcc gcc-c++ make python3 python3-pip rpm-build \
      openssl-devel libcurl-devel libicu-devel glib2-devel dbus-devel \
      fontconfig freetype mesa-libGL-devel \
      libxkbcommon-x11 xcb-util-wm xcb-util-keysyms xcb-util-renderutil xcb-util-cursor \
      ninja-build zlib-devel git pkgconf-pkg-config wget ca-certificates file which
    dnf clean all
    ;;
  opensuse-leap)
    zypper --non-interactive refresh
    zypper --non-interactive install --no-recommends \
      gcc15 gcc15-c++ make python3 python3-pip python3-virtualenv rpm-build \
      boost-devel libopenssl-devel libcurl-devel libicu-devel glib2-devel dbus-1-devel \
      fontconfig freetype2 libGL1 libGLU1 libxkbcommon-x11-0 \
      libxcb-icccm4 libxcb-keysyms1 libxcb-render-util0 libxcb-cursor0 \
      ragel ninja zlib-devel git pkg-config wget ca-certificates file which
    # Qt explicitly lists GCC 15 for openSUSE 16.0.
    # CI mounts the checkout at /usr/local, so selectors must live outside it.
    mkdir -p /opt/bin
    ln -s /usr/bin/gcc-15 /opt/bin/gcc
    ln -s /usr/bin/g++-15 /opt/bin/g++
    zypper clean --all
    ;;
  *) echo "Unsupported Qt 6.12 container distribution: $ID $VERSION_ID" >&2; exit 1 ;;
esac

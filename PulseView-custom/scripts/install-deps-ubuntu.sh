#!/usr/bin/env bash
# Run explicitly on Ubuntu 22.04/24.04 when setting up another machine.
set -euo pipefail
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential git pkg-config python3 \
    autoconf automake libtool autoconf-archive doxygen cmake ninja-build ripgrep \
    libglib2.0-dev libglibmm-2.4-dev libzip-dev python3-dev qtbase5-dev \
    qttools5-dev qttools5-dev-tools libqt5svg5-dev libboost-filesystem-dev \
    libboost-serialization-dev gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-dev

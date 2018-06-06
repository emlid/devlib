# devlib 

[![CodeFactor](https://www.codefactor.io/repository/github/emlid/devlib/badge)](https://www.codefactor.io/repository/github/emlid/devlib)
[![Releases](https://img.shields.io/github/release/emlid/devlib.svg)](https://github.com/emlid/devlib/releases)
[![Build Status](https://travis-ci.org/emlid/devlib.svg?branch=master)](https://travis-ci.org/emlid/devlib)
[![Build status](https://ci.appveyor.com/api/projects/status/7a03a27884mxbon1/branch/master?svg=true)](https://ci.appveyor.com/project/EmlidBuilderBot/devlib/branch/master)

> Obtaining info about storage devices

***

## General functions

+ Get VID and PID of storage device
+ Get mountpoints list (paths)
+ Get partitions list (paths and labels)
+ Get relations between partitions and mountpoints
+ Mounting/Unmounting
+ Interface for I/O ops with storage devices

## Supported Operating Systems

+ Linux (most distros)
+ Microsoft Windows 7 and later
+ MacOSX

## Build

### C++

The project is written on ```C++14```

### Compiler

| OS | Requirements |
| ------ | ------ |
| **Linux** | `g++5` or higher, 64 bit |
| **Windows** | [msvc2015](http://www.visualstudio.com/downloads/download-visual-studio-vs#d-express-windows-desktop), 32 bit |
| **OSX** | `clang++`, 64 bit |

### Dependencies

#### Qt

+ Download the [Qt installer](http://www.qt.io/download-open-source)
+ You need to install `Qt 5.9.3` or higher, and following packages:
  + Qt Core for your compiler (msvc2015/g++/clang++)

#### Additional packages

| OS | Packages |
| ------ | ------ |
| **Linux** | `sudo apt-get install libblkid-dev libudev-dev` |

### Make

+ From directory with project (use shadow build), type in terminal:
  + sh
    ```bash
    cd ..
    mkdir devlib-build
    cd devlib-build
    qmake "../devlib/devlib.pro"
    make
    ```

  + pwsh (PowerShell)
    ```bash
    cd ..
    mkdir devlib-build
    cd devlib-build
    qmake "..\devlib\devlib.pro"
    jom.exe
    ```

+ **Note**: By default ``qmake`` locates in ``[QTPATH]/[QTVERSION]/[COMPILER]/bin``. For example ```~/Qt/5.9.1/clang_64/bin/qmake```. On Windows, instead of make you can use ```jom.exe``` which installs with msvc compiler.

+ **ccache**: For building with ccache add `QMAKE_CXX='ccache $${QMAKE_CXX}'` to the end of qmake or QtCreator build kit

## Build configuration

By default the project builds with ``devlib`` library and ``examples``

### Options

+ ``EXCLUDE_EXAMPLES_BUILD`` - skip ``examples`` build
+ ``ENABLE_HEADERS_COPY`` - ``devlib`` builds with public headers (will be located in ``include`` dir)

  Example:

  ```bash
    qmake CONFIG+=EXCLUDE_EXAMPLES_BUILD \
          CONFIG+=ENABLE_HEADERS_COPY
  ```
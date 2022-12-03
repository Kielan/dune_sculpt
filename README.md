# dune sculpt
> Dune sculpt, Blender3d in c-lang currently in active development.

## Table of Contents

- [Background](#background)
- [Install](#install)
- [Usage](#usage)
- [Development setup](#development-setup)
    - [Guide](#guide)
    - [Code style](#code-style)
    - [Tests](#tests)
- [Contributors](#contributors)
- [Contribute](#contribute)
- [Communication](#communication)
- [License](#license)


## Background
Blender3d is written in using C/C++ and Python and code required handles for the 3 languages. This is a concise rewrite and cleanup.

## Install
This project uses CMake on Unix-like and MacOS. Go check them out if you don't have them locally installed.
```sh
$ sudo apt install
$ git clone https://github.com/
$ cd dune
$ make install
```

### Code style
Follow PEP 8
```sh
autopep8 --in-place --aggressive --max-line-length=88 <filename>
```

### Tests
Using `ctest`.

## Contributors

## Dune Sculpt
### Pre-Pre-Alpha
> Dune Sculpt, Blender3d in c-lang currently in active development. Blender without the Python bridge.
> This project is currently in pre-pre-alpha until buttons and tests are complete use is not reccomended.
> If you are interested in actively developing please do! This project is as open source as Linux meaning
> once you fork, it's your version for everyone to use and change as a person desires.

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
Including Sequencer for undo/redo, in source cachelimiter for gpu management and a C-lang only rewrite of GHOST (General Handy Operating System.)
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

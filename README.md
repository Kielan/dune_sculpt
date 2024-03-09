## Dune Sculpt
### Pre-Pre-Alpha
#### Next official update is planned for December 2024!
> Dune Sculpt, Blender3d in c-lang currently in active development. Blender without the Python bridge.
> This project is currently in pre-pre-alpha until buttons and tests are complete use is not reccomended.
> If you are interested in actively developing please do! This project is as open source as Linux meaning
> once you fork, it's your version for everyone to use and change as a person desires.

The intent is modularized package management like node vscode or emacs with a self documenting manual memory management scheme.
Think wave, but with an added dimension, it's like a dune. Originally when studying Blender I wanted to make a desert sand 3d graphics/physics sim generator;
Then I wanted a fourier transform, then a new garage band, and then I wanted sand and water sim, and now I want open source medical software to be available to everyone.
All these wants slowed and stalled development.

A lot of Blender conventions have been modified to be shorthand so that it's easier to code and read from your phone: 'but' change to 'btn' ect.
I hope that eventually I can stracture a git tree of pull requests so that official Blender3d will adopt some of the more terse conventions and if you
are part of the core Blender3d team please do so yourself and take all that work into your project before I get a chance to.

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

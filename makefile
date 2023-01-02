# This makefile is not meant for Windows
ifeq ($(OS),Windows_NT)
	$(error On Windows, use "cmd //c make.bat" instead of "make")
endif

# System Vars
OS:=$(shell uname -s)
OS_NCASE:=$(shell uname -s | tr '[A-Z]' '[a-z]')
CPU:=$(shell uname -m)


# Source and Build DIR's
DUNE_DIR:=$(shell pwd -P)
BUILD_TYPE:=Release

# CMake arguments, assigned to local variable to make it mutable.
CMAKE_CONFIG_ARGS := $(BUILD_CMAKE_ARGS)

ifndef BUILD_DIR
	BUILD_DIR:=$(shell dirname "$(DUNE_DIR)")/build_$(OS_NCASE)
endif

# Dependencies DIR's
DEPS_SOURCE_DIR:=$(DUNE_DIR)/build_files/build_environment

ifndef DEPS_BUILD_DIR
	DEPS_BUILD_DIR:=$(BUILD_DIR)/deps
endif

# path for Dune binary
ifeq ($(OS), Darwin)
	DUNE_BIN?="$(BUILD_DIR)/bin/Dune.app/Contents/MacOS/Dune"
else
	DUNE_BIN?="$(BUILD_DIR)/bin/dune"
endif

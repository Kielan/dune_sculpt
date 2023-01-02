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

BUILD_COMMAND:=make -s

# Build Dune
all: .FORCE
	@echo
	@echo Configuring Dune in \"$(BUILD_DIR)\" ...

#	# if test ! -f $(BUILD_DIR)/CMakeCache.txt ; then \
#	# 	$(CMAKE_CONFIG); \
#	# fi

#	# do this always incase of failed initial build
	@$(CMAKE_CONFIG)

	@echo
	@echo Building Dune ...
	$(BUILD_COMMAND) -C "$(BUILD_DIR)" -j $(NPROCS) install
	@echo
	@echo edit build configuration with: "$(BUILD_DIR)/CMakeCache.txt" run make again to rebuild.
	@echo Dune successfully built, run from: $(DUNE_BIN)
	@echo


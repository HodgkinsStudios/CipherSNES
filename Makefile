# CipherSNES — C++17 Linux source build (emulator and core only)
.DEFAULT_GOAL := all
CXX ?= g++
AR ?= ar
CXXFLAGS ?= -O3 -flto=auto
WARN := -std=c++17 -Wall -Wextra -Wpedantic
INCLUDES := -Iinclude

ARCHIVE_FLAGS := $(shell pkg-config --cflags libarchive 2>/dev/null)
ARCHIVE_LIBS := $(shell pkg-config --libs libarchive 2>/dev/null)
GTK_FLAGS := $(shell pkg-config --cflags gtk+-3.0 sdl2 2>/dev/null)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0 sdl2 2>/dev/null)

CORE_SOURCES := $(wildcard src/backend/core/*.cpp src/backend/util/*.cpp src/core/*.cpp)
CORE_OBJECTS := $(patsubst %.cpp,build/%.o,$(CORE_SOURCES))
UI_SOURCES := $(wildcard src/frontend/*.cpp)
UI_OBJECTS := $(patsubst %.cpp,build/%.o,$(UI_SOURCES))
LIBRARY := lib/libCipherSNESCore.a
SHARED := lib/libCipherSNESCore.so

.PHONY: FORCE all launcher launcher-x11 launcher-gtk core shared check-deps release-core release-launcher clean
FORCE:
# Rebuild affected objects if compiler version or flags change (important with LTO).
build/.compiler-config: FORCE
	@mkdir -p build
	@{ printf 'compiler=%s\n' '$(CXX)'; $(CXX) -dumpfullversion -dumpversion | sed 's/^/version=/'; printf 'flags=%s\n' '$(CXXFLAGS)'; } > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi

all: launcher
launcher launcher-x11: bin/CipherSNES bin/CipherSNES-NativePicker
core: $(LIBRARY) $(SHARED)
shared: $(SHARED)

build/%.o: %.cpp build/.compiler-config
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) $(ARCHIVE_FLAGS) -fPIC -MMD -MP -c $< -o $@

$(LIBRARY): $(CORE_OBJECTS)
	@mkdir -p lib
	$(AR) rcs $@ $^

$(SHARED): $(CORE_OBJECTS)
	@mkdir -p lib
	$(CXX) -shared -Wl,-soname,libCipherSNESCore.so $(CXXFLAGS) $^ -o $@ $(ARCHIVE_LIBS)

bin/CipherSNES: src/frontend_x11.cpp $(LIBRARY) build/.compiler-config
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) $< $(LIBRARY) -o $@ -lX11 -ldl $(ARCHIVE_LIBS)

bin/CipherSNES-NativePicker: src/frontend_native_picker.cpp build/.compiler-config
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(WARN) $< -o $@ -ldl

check-deps:
	@pkg-config --exists gtk+-3.0 sdl2 libarchive || { echo 'Optional GTK frontend requires libgtk-3-dev, libsdl2-dev, libarchive-dev'; exit 1; }

launcher-gtk: bin/CipherSNES-GTK
build/src/frontend/%.o: src/frontend/%.cpp build/.compiler-config | check-deps
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(WARN) $(INCLUDES) $(GTK_FLAGS) -fPIC -MMD -MP -c $< -o $@

bin/CipherSNES-GTK: $(UI_OBJECTS) $(LIBRARY) | check-deps
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(WARN) $^ -o $@ $(GTK_LIBS) $(ARCHIVE_LIBS)

release-core:
	$(MAKE) clean
	$(MAKE) -j$${JOBS:-2} CXXFLAGS='-O3 -flto=auto' core

release-launcher:
	$(MAKE) clean
	$(MAKE) -j$${JOBS:-2} CXXFLAGS='-O3 -flto=auto' launcher

clean:
	rm -rf build bin lib

-include $(CORE_OBJECTS:.o=.d) $(UI_OBJECTS:.o=.d)

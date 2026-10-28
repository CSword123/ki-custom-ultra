# ki-custom-ultra
#
#   make            build ./ki-custom-ultra (Dear ImGui + SDL3) with ki1-port's
#                   runtime linked in ($(KI1_PORT)/build/libki1.a, made there
#                   by `make lib`), and build/recomp/: ki1-port's recompiler
#                   and what it needs. No game code or data: the game code is
#                   recompiled from the user's U98 on the player's machine and
#                   compiled there with the player's gcc (MinGW on Windows)
#                   into data/gen/game.so / game.dll (src/kcu_host.h)
#   make run        start the editor
#   make catalog    remake data/catalog.json (the editor also does when
#                   tools/catalog.py, kiultra.py or movenames.py change)
#   make dist       dist/ki-custom-ultra/: the program and its tools, no game
#                   data (players add kinst.zip and kinst.chd in roms/)
#   make windows    cross-build build-win/ki-custom-ultra.exe (+ SDL3.dll) with
#                   MinGW-w64 or llvm-mingw:
#                     make windows SDL3_DIR=/path/SDL3-x.y.z/x86_64-w64-mingw32
#                   (the x86_64-w64-mingw32 folder of SDL's "SDL3-devel-*-mingw"
#                   release; CROSS, default x86_64-w64-mingw32-, prefixes g++;
#                   without SDL3_DIR, $(CROSS)pkg-config is asked)
#
# On Windows itself (MSYS2 UCRT64 shell with the packages
# mingw-w64-ucrt-x86_64-{gcc,SDL3,pkgconf,python} and make) plain `make`
# builds build-win/ki-custom-ultra.exe; run it from this folder (it uses
# build/ and tools/ relative to it). Python for tools/kiultra.py: $KI_PYTHON,
# else python3 (python on Windows).
#
# The game data is the user's own: kinst.zip and kinst.chd in roms/, checked
# and unpacked by the editor into data/ ($KI1_DATA), the game segments
# unpacked from their U98 (build/py/u98pack.py), the game code recompiled
# from those (build/recomp/recomp.py, into data/gen/) and the move catalog
# made from them (tools/catalog.py). Nothing here or in the program has them.

ifeq ($(OS),Windows_NT)
TARGET ?= windows
CROSS  ?=
else
TARGET ?= native
CROSS  ?= x86_64-w64-mingw32-
endif

IMGUI    := third_party/imgui
KI1_PORT ?= ../ki1-port

ifeq ($(TARGET),windows)
OBJDIR   := build-win/obj
PROG     := build-win/ki-custom-ultra.exe
CXX      := $(CROSS)g++
CC       := $(CROSS)gcc
PORT_BUILD := build-win
ifneq ($(SDL3_DIR),)
SDL_CFLAGS := -I$(SDL3_DIR)/include
SDL_LIBS   := -L$(SDL3_DIR)/lib -lSDL3
SDL_DLL    := $(SDL3_DIR)/bin/SDL3.dll
else
SDL_CFLAGS := $(shell $(CROSS)pkg-config --cflags sdl3)
SDL_LIBS   := $(shell $(CROSS)pkg-config --libs sdl3)
SDL_DLL    := $(firstword $(wildcard $(dir $(shell which $(CXX) 2>/dev/null))SDL3.dll))
endif
# a GUI program (no console window); the C++ runtime linked in
PLAT_LIBS := -mwindows -static-libgcc -static-libstdc++
else
OBJDIR   := build/obj
PROG     := ki-custom-ultra
CXX      ?= c++
CC       ?= cc
PORT_BUILD := build
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)
PLAT_LIBS :=
endif

CXXFLAGS := -O2 -g -std=c++17 -Wall -Wextra -I$(IMGUI) -I$(IMGUI)/backends -Ithird_party \
            $(SDL_CFLAGS)
CFLAGS   := -O2 -g -std=gnu11 -Wall -Wextra -I$(KI1_PORT)/include -I$(KI1_PORT)/src $(SDL_CFLAGS)
KI1_LIB  := $(KI1_PORT)/$(PORT_BUILD)/libki1.a
LDLIBS   := $(KI1_LIB) $(SDL_LIBS) -lm $(PLAT_LIBS)

IMGUI_SRC := $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp \
             $(IMGUI)/imgui_widgets.cpp $(IMGUI)/backends/imgui_impl_sdl3.cpp \
             $(IMGUI)/backends/imgui_impl_sdlrenderer3.cpp
OBJS := $(OBJDIR)/main.o $(OBJDIR)/preview.o $(OBJDIR)/engine.o $(IMGUI_SRC:$(IMGUI)/%.cpp=$(OBJDIR)/imgui/%.o)

PYTHON ?= python3

# release version (VERSION), shown by --version and in the window title
VERSION := $(shell cat VERSION)
CXXFLAGS += -DKCU_VERSION='"$(VERSION)"'

PY_COPIES := build/py/moves.py build/py/u98pack.py build/py/layout.py

# what recompiling the game code on the player's machine needs: ki1-port's
# recompiler, seg0's function starts (addresses, from ki1-decomp's asm), its
# config (entries, hooks, probes), and for compiling it the headers it
# includes and the glue to the editor (src/gameglue.c, src/kcu_host.h)
RECOMP := build/recomp
RECOMP_FILES := $(RECOMP)/recomp.py $(RECOMP)/functions.txt \
                $(RECOMP)/entries.txt $(RECOMP)/hooks.txt $(RECOMP)/probes.txt \
                $(patsubst $(KI1_PORT)/include/%,$(RECOMP)/include/%,$(wildcard $(KI1_PORT)/include/ki1/*.h)) \
                $(RECOMP)/glue.c $(RECOMP)/include/kcu_host.h

.PHONY: all run catalog clean windows dist FORCE
all: $(PROG) $(PY_COPIES) $(RECOMP_FILES)

$(RECOMP)/recomp.py: $(KI1_PORT)/tools/recomp.py
	@mkdir -p $(dir $@)
	cp $< $@
$(RECOMP)/functions.txt: FORCE
	$(MAKE) -C $(KI1_PORT) functions
	@mkdir -p $(dir $@)
	cmp -s $(KI1_PORT)/build/functions.txt $@ || cp $(KI1_PORT)/build/functions.txt $@
$(RECOMP)/%.txt: $(KI1_PORT)/config/%.txt
	@mkdir -p $(dir $@)
	cp $< $@
$(RECOMP)/include/ki1/%.h: $(KI1_PORT)/include/ki1/%.h
	@mkdir -p $(dir $@)
	cp $< $@
$(RECOMP)/glue.c: src/gameglue.c
	@mkdir -p $(dir $@)
	cp $< $@
$(RECOMP)/include/kcu_host.h: src/kcu_host.h
	@mkdir -p $(dir $@)
	cp $< $@

# ki1-decomp's move-data reader and U98 packer, for tools/kiultra.py and
# tools/catalog.py where ki1-decomp is absent, and for unpacking the game
# segments from the user's U98
build/py/%.py: ../ki1-decomp/tools/%.py
	@mkdir -p $(dir $@)
	cp $< $@

# ki1-port's own make decides whether the library is up to date
$(KI1_LIB): FORCE
	$(MAKE) -C $(KI1_PORT) lib TARGET=$(TARGET) $(if $(SDL3_DIR),SDL3_DIR=$(abspath $(SDL3_DIR))) \
		$(if $(filter windows,$(TARGET)),CROSS=$(CROSS))

$(PROG): $(OBJS) $(KI1_LIB)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(OBJS) $(LDLIBS)
ifeq ($(TARGET),windows)
	$(if $(SDL_DLL),cp $(SDL_DLL) $(dir $@),@echo "note: put SDL3.dll next to $@")
endif

$(OBJDIR)/%.o: src/%.cpp src/preview.h src/engine.h VERSION
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/engine.o: src/engine.c src/engine.h src/kcu_host.h $(wildcard $(KI1_PORT)/src/*.h $(KI1_PORT)/include/ki1/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/imgui/%.o: $(IMGUI)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -w -c $< -o $@

catalog: $(PY_COPIES)
	$(PYTHON) tools/catalog.py --seg1 $${KI1_DATA:-data}/seg1.bin -o $${KI1_DATA:-data}/catalog.json

run: all
	./$(PROG)

# cross-build from Linux into build-win/
windows:
	$(MAKE) TARGET=windows

# what can be handed out: the program, its Python tools, the recompiler and
# roms/ (empty but for its README); no game code or data, segments, catalog
# or compiled patches
DIST := dist/ki-custom-ultra
dist: $(PROG) $(PY_COPIES) $(RECOMP_FILES)
	rm -rf $(DIST)
	mkdir -p $(DIST)/tools $(DIST)/build/py $(DIST)/roms $(DIST)/projects
	cp -r $(RECOMP) $(DIST)/build/
	cp $(PROG) $(DIST)/
	$(if $(filter windows,$(TARGET)),cp $(dir $(PROG))SDL3.dll $(DIST)/)
	cp tools/*.py tools/crashes.json $(DIST)/tools/
	cp $(PY_COPIES) $(DIST)/build/py/
	cp roms/README.txt $(DIST)/roms/
	cp README.md VERSION $(DIST)/

clean:
	rm -rf build/obj build-win ki-custom-ultra dist

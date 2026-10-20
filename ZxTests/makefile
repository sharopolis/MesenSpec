#Headless tests for the ZX Spectrum core - only needs Core/ZX and a few Utilities, no SDL/.NET
#Run from the repository root: make -f ZxTests/makefile test

CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -O2 -g
CXXFLAGS += -Wall --std=c++17 -I . -I Core -I Utilities -MMD -MP

#The standalone machine only - ZxConsole/ZxControlManager connect it to the Mesen emulator and need the whole core
ZXSRC := $(filter-out Core/ZX/ZxConsole.cpp Core/ZX/ZxControlManager.cpp,$(shell find Core/ZX -name '*.cpp'))
ZXOBJ := $(ZXSRC:Core/%.cpp=ZxTests/obj/%.o)

ZxTests/obj/%.o: Core/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

ZxTests/obj/%.o: ZxTests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

#Utility sources the tools need, built here (the main build's objects may be clang LTO bitcode)
UTILSRC := Utilities/Serializer.cpp Utilities/miniz.cpp Utilities/Audio/blip_buf.cpp
PNGSRC := Utilities/PNGHelper.cpp Utilities/spng.c
UTILOBJ := $(UTILSRC:%.cpp=ZxTests/obj/%.o)
PNGOBJ := ZxTests/obj/Utilities/PNGHelper.o ZxTests/obj/Utilities/spng.o

ZxTests/obj/Utilities/%.o: Utilities/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -w -c $< -o $@

ZxTests/obj/Utilities/%.o: Utilities/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -w -MMD -MP -I Utilities -c $< -o $@

ZxTests/FuseZ80Test: ZxTests/obj/FuseZ80Test.o $(ZXOBJ) $(UTILOBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

ZxTests/ZxRun: ZxTests/obj/ZxRun.o $(ZXOBJ) $(UTILOBJ) $(PNGOBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

ZxTests/ZxMachineTest: ZxTests/obj/ZxMachineTest.o $(ZXOBJ) $(UTILOBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

all: ZxTests/FuseZ80Test ZxTests/ZxRun ZxTests/ZxMachineTest

#The machine tests need a 48K ROM, which is not in the repository (put Fuse's roms/48.rom in ZxTests/roms, or pass ZXROM=path)
ZXROM ?= $(wildcard ZxTests/roms/48.rom)
test: ZxTests/FuseZ80Test ZxTests/ZxMachineTest
	ZxTests/FuseZ80Test ZxTests/fuse/tests.in ZxTests/fuse/tests.expected
	@if [ -n "$(ZXROM)" ]; then ZxTests/ZxMachineTest "$(ZXROM)"; else echo "ZXROM not set - skipping the machine tests"; fi

-include $(shell find ZxTests/obj -name '*.d' 2>/dev/null)

clean:
	rm -rf ZxTests/obj ZxTests/FuseZ80Test ZxTests/ZxRun ZxTests/ZxMachineTest

.PHONY: all test clean

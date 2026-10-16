#Headless tests for the ZX Spectrum core - only needs Core/ZX and Utilities, no SDL/.NET
#Run from the repository root: make -f ZxTests/makefile test

CXX ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -Wall --std=c++17 -I . -I Core -I Utilities -MMD -MP

ZXSRC := $(shell find Core/ZX -name '*.cpp')
ZXOBJ := $(ZXSRC:Core/%.cpp=ZxTests/obj/%.o)

ZxTests/obj/%.o: Core/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

ZxTests/obj/%.o: ZxTests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

#Serializer and its dependencies, built by the main makefile
UTILOBJ := $(addprefix Utilities/,Serializer.o miniz.o)
PNGOBJ := $(addprefix Utilities/,PNGHelper.o spng.o)

ZxTests/FuseZ80Test: ZxTests/obj/FuseZ80Test.o $(ZXOBJ) $(UTILOBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

ZxTests/ZxRun: ZxTests/obj/ZxRun.o $(ZXOBJ) $(UTILOBJ) $(PNGOBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

ZxTests/ZxMachineTest: ZxTests/obj/ZxMachineTest.o $(ZXOBJ) $(UTILOBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

all: ZxTests/FuseZ80Test ZxTests/ZxRun ZxTests/ZxMachineTest

#The machine tests need a 48K ROM (not included in the repository): make -f ZxTests/makefile test ZXROM=path/to/48.rom
test: ZxTests/FuseZ80Test ZxTests/ZxMachineTest
	ZxTests/FuseZ80Test ZxTests/fuse/tests.in ZxTests/fuse/tests.expected
	@if [ -n "$(ZXROM)" ]; then ZxTests/ZxMachineTest "$(ZXROM)"; else echo "ZXROM not set - skipping the machine tests"; fi

-include $(shell find ZxTests/obj -name '*.d' 2>/dev/null)

clean:
	rm -rf ZxTests/obj ZxTests/FuseZ80Test ZxTests/ZxRun ZxTests/ZxMachineTest

.PHONY: all test clean

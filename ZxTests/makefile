#Headless tests for the ZX Spectrum core - only needs Core/ZX and Utilities, no SDL/.NET
#Run from the repository root: make -f ZxTests/makefile test

CXX ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -Wall --std=c++17 -I . -I Core -I Utilities

ZXSRC := $(shell find Core/ZX -name '*.cpp')
ZXOBJ := $(ZXSRC:Core/%.cpp=ZxTests/obj/%.o)

ZxTests/obj/%.o: Core/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

ZxTests/obj/FuseZ80Test.o: ZxTests/FuseZ80Test.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

#Serializer and its dependencies, built by the main makefile
UTILOBJ := $(addprefix Utilities/,Serializer.o miniz.o)

ZxTests/FuseZ80Test: ZxTests/obj/FuseZ80Test.o $(ZXOBJ) $(UTILOBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

test: ZxTests/FuseZ80Test
	ZxTests/FuseZ80Test ZxTests/fuse/tests.in ZxTests/fuse/tests.expected

clean:
	rm -rf ZxTests/obj ZxTests/FuseZ80Test

.PHONY: test clean

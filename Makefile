CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -DNDEBUG -march=native -pthread
CPPFLAGS ?= -I../deps/chess-library -I../deps/incbin

SOURCES := $(wildcard src/*.cpp)
HEADERS := $(wildcard src/*.h deps/*/*.h)
NNUE := nnue/selfgen-v2.bin
TARGET := build/amalgam.exe

ifeq ($(OS),Windows_NT)
MKDIR = if not exist build mkdir build
CLEAN = if exist $(TARGET) del /q $(TARGET)
else
MKDIR = mkdir -p build
CLEAN = rm -f $(TARGET)
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): Makefile $(SOURCES) $(HEADERS) $(NNUE)
	@$(MKDIR)
	cd build && $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(addprefix ../,$(SOURCES)) -o amalgam.exe

clean:
	$(CLEAN)

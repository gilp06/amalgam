CXX = g++
CXXFLAGS = -std=c++17 -O3 -DNDEBUG -march=native -pthread
CPPFLAGS = -Isrc -Ideps/chess-library -Ideps/incbin

EXE ?= amalgam

SOURCES = $(wildcard src/*.cpp)
HEADERS = $(wildcard src/*.h)
TARGET = $(EXE)$(if $(filter Windows_NT,$(OS)),.exe,)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o $@

clean:
	rm -f $(TARGET)
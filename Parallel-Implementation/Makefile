# Makefile za paralelnu verziju (bez optimizacija)

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -O0 -pthread
OPENCV   = $(shell pkg-config --cflags --libs opencv4)

SRC  = main.cpp
BIN  = space-defender-par

all: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN) $(OPENCV)

clean:
	rm -f $(BIN)


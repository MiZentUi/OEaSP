CXX      := g++
CXXFLAGS := -O2 -std=c++17 -pthread -Wall -Wextra
TARGET   := main
SRC      := main.cpp
N        ?= 100000000

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET) $(N)

clean:
	rm -f $(TARGET)
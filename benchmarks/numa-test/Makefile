CXX = g++
CXXFLAGS = -O3 -std=c++17 -pthread -march=native

all: numa-test

numa-test: numa-test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f numa-test

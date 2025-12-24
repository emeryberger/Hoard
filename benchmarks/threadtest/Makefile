include ../Makefile.inc

TARGET = threadtest

$(TARGET): threadtest.cpp
	$(CXX) -std=c++17 -I../../src/Heap-Layers -DNOMALLOC=1 -DNDEBUG $(CXXFLAGS) threadtest.cpp -o $(TARGET)-null -lpthread
	$(CXX) -std=c++17 -I../../src/Heap-Layers -DNDEBUG $(CXXFLAGS) threadtest.cpp -o $(TARGET) -lpthread
#	$(CXX) -std=c++14 $(CXXFLAGS) -fsized-deallocation threadtest.cpp -o $(TARGET) -lpthread

clean:
	rm -f $(TARGET)

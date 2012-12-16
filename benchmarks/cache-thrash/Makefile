include ../Makefile.inc

TARGET = cache-thrash

$(TARGET): cache-thrash.cpp
	$(CXX) $(CXXFLAGS) cache-thrash.cpp -o $(TARGET) -lpthread

clean:
	rm -f $(TARGET)

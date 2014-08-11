include ../Makefile.inc

TARGET = threadtest

$(TARGET): threadtest.cpp
	$(CXX) -std=c++11 $(CXXFLAGS) threadtest.cpp -o $(TARGET) -lpthread

clean:
	rm -f $(TARGET)

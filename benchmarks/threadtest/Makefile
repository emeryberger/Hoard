include ../Makefile.inc

TARGET = threadtest

$(TARGET): threadtest.cpp
	$(CXX) -std=c++14 $(CXXFLAGS) threadtest.cpp -o $(TARGET) -lpthread
#	$(CXX) -std=c++14 $(CXXFLAGS) -fsized-deallocation threadtest.cpp -o $(TARGET) -lpthread

clean:
	rm -f $(TARGET)

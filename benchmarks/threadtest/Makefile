include ../Makefile.inc

TARGET = threadtest

$(TARGET): threadtest.cpp
	$(CXX) $(CXXFLAGS) threadtest.cpp -o $(TARGET) -lpthread

clean:
	rm -f $(TARGET)

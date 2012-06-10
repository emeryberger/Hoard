include ../Makefile.inc

TARGET = shbench

$(TARGET): shbench1.cpp
	$(CXX) $(CXXFLAGS) shbench1.cpp -o $(TARGET) -lpthread

clean:
	rm -f $(TARGET)

CC  := $(CC)
CXX := $(CXX)

CCFLAGS  := -g -O3 -DNDEBUG -I../common
CXXFLAGS := -g -O3 -DNDEBUG -I../common

TARGET = mtest

$(TARGET): mtest.cpp
	$(CXX) $(CXXFLAGS) mtest.cpp -o $(TARGET) -lpthread

clean:
	rm -f $(TARGET)

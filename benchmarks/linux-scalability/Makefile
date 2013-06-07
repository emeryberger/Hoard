include ../Makefile.inc

TARGET = linux-scalability

$(TARGET): $(TARGET).c
	$(CC) $(CCFLAGS) $(TARGET).c -o $(TARGET) -lpthread

clean:
	rm -f $(TARGET)

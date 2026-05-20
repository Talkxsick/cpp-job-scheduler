CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
LIBS = -lncursesw
SRC = src/main.cpp
TARGET = scheduler
 
build:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LIBS)
 
run: build
	./scheduler
 
clean:
	rm -f $(TARGET)
 
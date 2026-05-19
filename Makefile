CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
SRC = src/main.cpp
TARGET = scheduler
 
build:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)
 
run: build
	./scheduler
 
clean:
	rm -f $(TARGET)
 
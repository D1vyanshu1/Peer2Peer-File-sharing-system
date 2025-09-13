# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -pthread -std=c++11

# Target executable
TARGET = tracker

# Source and object files
SRCS = main.cpp fetchIP_Port.cpp
OBJS = $(SRCS:.cpp=.o)

# Header files
HEADERS = headher.h

# Default rule
all: $(TARGET)

# Link object files into the final executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Compile each .cpp into .o
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -f $(OBJS) $(TARGET)

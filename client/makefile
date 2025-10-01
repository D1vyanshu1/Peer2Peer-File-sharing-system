# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -pthread -std=c++11

# Target executable
TARGET = client

# Source and object files
SRCS = main.cpp fetchIP.cpp trackerConnection.cpp upload_file.cpp downloadChunk.cpp
OBJS = $(SRCS:.cpp=.o)

# Header files
HEADERS = cheadher.h

# Default rule
all: $(TARGET)

# Link object files into the final executable
# Add OpenSSL libs
LDLIBS = -lcrypto -lssl

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)


# Compile each .cpp into .o
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -f $(OBJS) $(TARGET)

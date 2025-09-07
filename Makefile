CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread
TARGET = app
OBJS = main.o NetworkManager.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

main.o: main.cpp NetworkManager.h
	$(CXX) $(CXXFLAGS) -c main.cpp

NetworkManager.o: NetworkManager.cpp NetworkManager.h
	$(CXX) $(CXXFLAGS) -c NetworkManager.cpp

clean:
	rm -f $(OBJS) $(TARGET)

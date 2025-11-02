CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -pthread
OBJS = main.o SubnetBroadcaster.o SubnetListener.o
TARGET = netdemo

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

main.o: main.cpp SubnetBroadcaster.hpp SubnetListener.hpp
	$(CXX) $(CXXFLAGS) -c main.cpp

SubnetBroadcaster.o: SubnetBroadcaster.cpp SubnetBroadcaster.hpp
	$(CXX) $(CXXFLAGS) -c SubnetBroadcaster.cpp

SubnetListener.o: SubnetListener.cpp SubnetListener.hpp
	$(CXX) $(CXXFLAGS) -c SubnetListener.cpp

clean:
	rm -f $(OBJS) $(TARGET)

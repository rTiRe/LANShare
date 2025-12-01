CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -pthread -fPIC
OBJS = main.o SubnetBroadcaster.o SubnetListener.o FileTransfer.o UI.o UIQt.o
TARGET = netdemo

CFLAGS_UI = -lncurses

QT_CFLAGS = $(shell pkg-config --cflags Qt5Widgets)
QT_LIBS = $(shell pkg-config --libs Qt5Widgets)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) $(CFLAGS_UI) $(QT_LIBS)

main.o: main.cpp SubnetBroadcaster.hpp SubnetListener.hpp
	$(CXX) $(CXXFLAGS) $(QT_CFLAGS) -c main.cpp

SubnetBroadcaster.o: SubnetBroadcaster.cpp SubnetBroadcaster.hpp
	$(CXX) $(CXXFLAGS) -c SubnetBroadcaster.cpp

SubnetListener.o: SubnetListener.cpp SubnetListener.hpp
	$(CXX) $(CXXFLAGS) -c SubnetListener.cpp

FileTransfer.o: FileTransfer.cpp FileTransfer.hpp
	$(CXX) $(CXXFLAGS) -c FileTransfer.cpp

UIQt.o: UIQt.cpp UIQt.hpp
	$(CXX) $(CXXFLAGS) $(QT_CFLAGS) -c UIQt.cpp



clean:
	rm -f $(OBJS) $(TARGET)

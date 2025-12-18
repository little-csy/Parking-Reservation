# Makefile for EE450 Parking Reservation Project (Skeleton)

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra

TARGETS = client serverM serverA serverR serverP

all: $(TARGETS)

client: client.cpp
	$(CXX) $(CXXFLAGS) -o client client.cpp

serverM: serverM.cpp
	$(CXX) $(CXXFLAGS) -o serverM serverM.cpp

serverA: serverA.cpp
	$(CXX) $(CXXFLAGS) -o serverA serverA.cpp

serverR: serverR.cpp
	$(CXX) $(CXXFLAGS) -o serverR serverR.cpp

serverP: serverP.cpp
	$(CXX) $(CXXFLAGS) -o serverP serverP.cpp

clean:
	rm -f $(TARGETS)

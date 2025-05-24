CXX = g++
CXXFLAGS = -O2 -Wall
LDFLAGS = -lpng
TARGET = png2curcuitmaker
SRC = png2curcuitmaker.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

install: $(TARGET)
	sudo cp $(TARGET) /usr/bin/

clean:
	rm -f $(TARGET)
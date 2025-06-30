CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 
TARGET = main
SRC = $(wildcard *.cc)
OBJ = $(SRC:.cc=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(FLAG) $(TARGET) $(OBJ)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 新增 target: init-disk
# 使用方式: make init-disk FILE=disk.img SIZE=100M
init-disk:
ifndef FILE
	$(error FILE is undefined. Use FILE=<filename>)
endif
ifndef SIZE
	$(error SIZE is undefined. Use SIZE=<size> (e.g. 100M, 1G))
endif
	@echo "Creating raw disk image: $(FILE) with size $(SIZE)"
	truncate -s $(SIZE) $(FILE)

clean:
	rm -rf build

build:
	cmake -S . -B build
	cmake --build build -- -j$(nproc)


# clean:
# 	rm -f $(OBJ) $(TARGET)

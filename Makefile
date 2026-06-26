CC := gcc
CXX := g++

TARGET := engine

CPP_DIR := cpp_layer
SRC_DIR := src
OBJ_DIR := obj
INC_DIR := include

CPP_SRCS := $(shell find $(CPP_DIR) -type f -name '*.cpp')
CPP_OBJS := $(patsubst $(CPP_DIR)/%.cpp,$(OBJ_DIR)/cpp/%.o,$(CPP_SRCS))

SRCS := $(shell find $(SRC_DIR) -type f -name '*.c')
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/c/%.o,$(SRCS))

CXXFLAGS := -g
CFLAGS := -Wall -Wextra -g
CPPFLAGS := -I$(INC_DIR)

LDFLAGS :=
LDLIBS := -lvolk -lvulkan -lSDL3 -lstdc++ -lshaderc_shared -lm

all: $(OBJ_DIR) $(TARGET)

$(TARGET): $(OBJS) $(CPP_OBJS)
	@$(CC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/c
	@mkdir -p $(OBJ_DIR)/cpp

$(OBJ_DIR)/c/%.o: $(SRC_DIR)/%.c
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cpp/%.o: $(CPP_DIR)/%.cpp
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

run: all
	@./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

compdb:
	bear -- make clean all

.PHONY: compdb all clean run

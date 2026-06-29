CC := gcc
CXX := g++

TARGET := engine

CPP_DIR := cpp_layer
SRC_DIR := src
DEP_DIR	:= deps
SHADER_DIR := shaders
COMPILED_SHADER_DIR := $(SHADER_DIR)/compiled
OBJ_DIR := obj
INC_DIR := include

CPP_SRCS := $(shell find $(CPP_DIR) -type f -name '*.cpp')
CPP_OBJS := $(patsubst $(CPP_DIR)/%.cpp,$(OBJ_DIR)/cpp/%.o,$(CPP_SRCS))

SRCS := $(shell find $(SRC_DIR) -type f -name '*.c')
SRCS += $(shell find $(DEP_DIR) -type f -name '*.c')
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/c/%.o,$(SRCS))

SHADER_SRCS := $(shell find $(SHADER_DIR) -type f -name '*.slang')
SHADERS := $(COMPILED_SHADER_DIR)/slang.spv
ENTRY_POINTS := -entry vertMain -entry fragMain
SLANGC := tools/slang/bin/slangc

CXXFLAGS := -g
CFLAGS := -Wall -Wextra -g
CPPFLAGS := -I$(INC_DIR) -I$(DEP_DIR)

LDFLAGS :=
LDLIBS := -lvolk -lvulkan -lSDL3 -lstdc++ -lshaderc_shared -lm

all: $(OBJ_DIR) $(COMPILED_SHADER_DIR) $(TARGET) $(SHADERS)

$(COMPILED_SHADER_DIR):
	@mkdir -p shaders/compiled

$(SHADERS): $(SHADER_SRCS)
	$(SLANGC) $(SHADER_SRCS) -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o $(COMPILED_SHADER_DIR)/slang.spv

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

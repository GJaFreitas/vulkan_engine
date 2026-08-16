CC := gcc
CXX := g++

TARGET := engine.out

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

MYLIB_DIR := MyLib
MYLIB_INC := $(MYLIB_DIR)/include
MYLIB_LIB := $(MYLIB_DIR)/mylib.a

SHADER_SRCS := $(shell find $(SHADER_DIR) -maxdepth 1 -type f -name '*.slang')
SHADERS := $(patsubst $(SHADER_DIR)/%.slang,$(COMPILED_SHADER_DIR)/%.spv,$(SHADER_SRCS))
SHADER_INC := -I$(SHADER_DIR)/include
ENTRY_POINTS := -entry vertMain -entry fragMain
SLANGC := slangc

VKCPKG_ROOT := vcpkg_installed
TRIPLET := x64-linux
VKPKG_INC := $(VKCPKG_ROOT)/$(TRIPLET)/include
VKPKG_LIB := $(VKCPKG_ROOT)/$(TRIPLET)/lib

CXXFLAGS := -g
CFLAGS := -Wall -Wextra -g
CPPFLAGS := -I$(INC_DIR) -I$(DEP_DIR) -I$(MYLIB_INC) -I$(VKPKG_INC)

LDFLAGS := -L$(MYLIB_DIR) -l:mylib.a -L$(VKPKG_LIB) -Wl,-rpath,$(VKPKG_LIB)
LDLIBS := -lvolk -lvulkan -lSDL3 -lstdc++ -lshaderc_shared -lm \
		-lharfbuzz -lfreetype  -lpng16 -lz -lbz2 -lbrotlidec -lbrotlicommon

all: $(OBJ_DIR) $(COMPILED_SHADER_DIR) $(SHADERS) $(MYLIB_LIB) $(TARGET)

$(MYLIB_LIB): FORCE
	@$(MAKE) -C $(MYLIB_DIR) --no-print-directory
FORCE:

$(COMPILED_SHADER_DIR):
	@mkdir -p shaders/compiled

$(COMPILED_SHADER_DIR)/%.spv: $(SHADER_DIR)/%.slang
	$(SLANGC) $< $(SHADER_INC) -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o $@

$(TARGET): $(OBJS) $(CPP_OBJS) $(MYLIB_LIB)
	$(CC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/c
	@mkdir -p $(OBJ_DIR)/cpp

$(OBJ_DIR)/c/%.o: $(SRC_DIR)/%.c
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cpp/%.o: $(CPP_DIR)/%.cpp
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

re:
	rm -rf $(OBJ_DIR)/c $(TARGET)
	@mkdir -p $(OBJ_DIR)/c
	make

run: all
	@./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(COMPILED_SHADER_DIR)

compdb:
	bear -- make clean all
	cd MyLib && bear -- make clean all

.PHONY: compdb all clean run re

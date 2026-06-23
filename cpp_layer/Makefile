VMA = vma.o

LIBS = -lvolk -lvulkan -lSDL3 -lstdc++ -lshaderc_shared

all: $(VMA)
	@clear
	@gcc -Wall -o triangle vulkan.c $(VMA) $(INCLUDE) $(LIBS)
	@./triangle

$(VMA): vma.cpp
	@echo "compiling vma"
	@g++ -c -o $(VMA) vma.cpp

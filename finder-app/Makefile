# Compiler and flags
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -g
CFLAGS += -Wimplicit-function-declaration

# File names
TARGET = writer
SRC = writer.c
OBJ = writer.o

# Default target: build the writer app
all: $(TARGET)

# Linking the object file to create the final executable
$(TARGET): $(OBJ)
	$(CC) -o $(TARGET) $(OBJ)

# Compilation rule: generate .o files from .c files
%.o: %.c
	$(CC) $(CFLAGS) -c $<

# Clean up build artifacts
clean:
	rm -f $(TARGET) $(OBJ)

# Phony targets (non-file targets)
.PHONY: all clean

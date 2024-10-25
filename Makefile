# Compiler and flags
CC = gcc
CFLAGS = -g -Wall -Werror

# Target executable
TARGET = mfs

# Source files
SRCS = mfs.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default rule to build the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Rule to compile .c files into .o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up the directory
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: clean


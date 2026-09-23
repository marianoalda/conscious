CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -I../conscious/src
LDLIBS = -lGL -lGLU -lX11 -lm

TARGET = build/heightmap-view
WORLD_SRCS = world.c world_io.c world_v0.c world_v1.c world_v2.c world_v3.c
WORLD_OBJS = $(WORLD_SRCS:%.c=build/%.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): build/heightmap_view.o $(WORLD_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/heightmap_view.o: src/heightmap_view.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: ../conscious/src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

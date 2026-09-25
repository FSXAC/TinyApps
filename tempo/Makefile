CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

TARGET := tempo

all: $(TARGET)

$(TARGET): src/main.c src/tempo.c src/tempo.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ src/main.c src/tempo.c $(SDL_LIBS)

tempo_test: tests/tempo_test.c src/tempo.c src/tempo.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/tempo_test.c src/tempo.c -lm

run: $(TARGET)
	./$(TARGET)

test: tempo_test
	./tempo_test

clean:
	rm -f $(TARGET) tempo_test

.PHONY: all run test clean

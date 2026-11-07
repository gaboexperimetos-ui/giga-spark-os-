# Native simulator. Override optimization with `make OPT=-O0` for debugging.
CC ?= gcc
OPT ?= -O3 -DNDEBUG
CFLAGS ?= -Wall -Wextra -Werror
CFLAGS += -std=c11 $(OPT) -I../common -I../sdk -I../common/include $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2) -lm
BIN = gigaspark_sim

.PHONY: all clean run test
all: $(BIN)
$(BIN): main.c ../common/framebuffer.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c $(LDLIBS)
run: $(BIN)
	./$(BIN)
test: $(BIN)
	SDL_VIDEODRIVER=dummy ./$(BIN) --smoke
clean:
	rm -f $(BIN)

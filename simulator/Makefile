# Simulador nativo. Usar `make OPT=-O0` para depurar.
#
# Detecta SDL3 (version actual) y cae a SDL2 si el sistema solo trae la 2.x.
# La API se adapta en sdl_compat.h.
#   make SDL=2                          # forzar SDL2
#   make SDL_PREFIX=/ruta/al/prefijo    # SDL instalado a mano, sin pkg-config
CC ?= gcc
OPT ?= -O3 -DNDEBUG
CFLAGS ?= -Wall -Wextra -Werror

# SDL_PREFIX permite apuntar a un SDL instalado a mano (sin pkg-config), que es
# como lo prepara tools/preview/bootstrap.sh en ~/.cache/giga-deps/install3.
SDL_PREFIX ?=
SDL ?= auto

ifeq ($(SDL),auto)
  ifneq ($(SDL_PREFIX),)
    SDL := $(shell test -f $(SDL_PREFIX)/include/SDL3/SDL.h && echo 3 || echo 2)
  else
    SDL := $(shell pkg-config --exists sdl3 2>/dev/null && echo 3 || echo 2)
  endif
endif

ifeq ($(SDL),3)
  SDL_DEF := GSP_USE_SDL3=1
  ifneq ($(SDL_PREFIX),)
    SDL_CFLAGS := -I$(SDL_PREFIX)/include
    SDL_LIBS := $(SDL_PREFIX)/lib/libSDL3.a -lpthread -ldl
  else
    SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
    SDL_LIBS := $(shell pkg-config --libs sdl3 2>/dev/null)
  endif
else
  SDL_DEF := GSP_USE_SDL3=0
  ifneq ($(SDL_PREFIX),)
    SDL_CFLAGS := $(shell $(SDL_PREFIX)/bin/sdl2-config --cflags)
    SDL_LIBS := $(shell $(SDL_PREFIX)/bin/sdl2-config --static-libs)
  else
    SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
    SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
  endif
endif

CFLAGS += -std=c11 $(OPT) -D$(SDL_DEF) \
          -I../common -I../sdk -I../common/include $(SDL_CFLAGS)
LDLIBS += $(SDL_LIBS) -lm
BIN = gigaspark_sim

.PHONY: all clean run test
all: $(BIN)
$(BIN): main.c ../common/ui_os.c ../common/ui_material.c sdl_compat.h \
        ../common/include/roboto_font.h ../common/framebuffer.h ../common/include/ui_theme.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c $(LDLIBS)
run: $(BIN)
	./$(BIN)
# SDL3 renombro la variable de entorno del driver de video; se pasan ambas.
test: $(BIN)
	SDL_VIDEODRIVER=dummy SDL_VIDEO_DRIVER=dummy ./$(BIN) --smoke
clean:
	rm -f $(BIN)

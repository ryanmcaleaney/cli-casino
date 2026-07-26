CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Isrc

SRC   := $(wildcard src/*.c) $(wildcard src/games/*.c)

# Optional raylib GUI: prefer the system raylib (pkg-config), fall back to
# a vendored copy in vendor/raylib.  Without either, the GUI is skipped
# and the CLI builds exactly as before.
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS   := $(shell pkg-config --libs raylib 2>/dev/null)
ifeq ($(strip $(RAYLIB_LIBS)),)
ifneq ($(wildcard vendor/raylib/lib/libraylib.a),)
RAYLIB_CFLAGS := -Ivendor/raylib/include
RAYLIB_LIBS   := vendor/raylib/lib/libraylib.a -lm -lpthread -ldl -lrt -lX11
endif
endif
ifneq ($(strip $(RAYLIB_LIBS)),)
SRC      += $(wildcard src/gui/*.c)
CPPFLAGS += -DCASINO_GUI $(RAYLIB_CFLAGS)
GUI_LIBS := $(RAYLIB_LIBS)
endif

OBJ   := $(patsubst src/%.c,build/%.o,$(SRC))
BIN   := casino
LINKS := roulette coin dice sicbo baccarat blackjack craps slots \
         videopoker ridethebus war threecard chuckaluck bigsix

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(GUI_LIBS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

symlinks: $(BIN)
	@for l in $(LINKS); do ln -sf $(BIN) $$l; done
	@echo "created: $(LINKS)"

test: $(BIN)
	sh tests/run.sh

clean:
	rm -rf build $(BIN) $(LINKS)

.PHONY: all symlinks test clean

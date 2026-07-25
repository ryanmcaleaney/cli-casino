CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Isrc

SRC   := $(wildcard src/*.c) $(wildcard src/games/*.c)
OBJ   := $(patsubst src/%.c,build/%.o,$(SRC))
BIN   := casino
LINKS := roulette coin dice sicbo baccarat blackjack craps slots \
         videopoker war threecard chuckaluck bigsix

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

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

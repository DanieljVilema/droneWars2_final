CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -pthread
LDFLAGS := -pthread

SRC_NET := src/net/heartbeat.c
SRC_CMD := src/command/reassemble.c
SRC_COMMON := $(SRC_NET) $(SRC_CMD)

BINARIES := main comando camion drone artilleria

all: $(BINARIES)

main: src/main.c $(SRC_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

comando: src/comando.c $(SRC_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

camion: src/camion.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

drone: src/drone.c $(SRC_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

artilleria: src/artilleria.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: all
	./main

clean:
	rm -f $(BINARIES) *.o

.PHONY: debug asan tsan test run-sim

debug: CFLAGS := -O0 -g -Wall -Wextra -Werror -pedantic -pthread
debug: LDFLAGS := -pthread
debug: clean all

asan: CFLAGS := -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic -pthread
asan: LDFLAGS := -fsanitize=address,undefined -pthread
asan: clean all

tsan: CFLAGS := -O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic -pthread
tsan: LDFLAGS := -fsanitize=thread -pthread
tsan: clean all

tests/test_reassemble: tests/test_reassemble.c $(SRC_COMMON)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

tests/test_fuel: tests/test_fuel.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

tests/test_heartbeat: tests/test_heartbeat.c $(SRC_NET)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

unit: tests/test_reassemble tests/test_fuel tests/test_heartbeat

tests/test_state: tests/test_state.c src/drone/state.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

unit: tests/test_reassemble tests/test_fuel tests/test_heartbeat tests/test_state

test: unit all
	@set -e; \
	./tests/test_reassemble; \
	./tests/test_fuel; \
	./tests/test_heartbeat; \
	./tests/test_state; \
	bash tests/test_integration.sh

run-sim: all
	@echo "Running with config.txt (SCENARIO ignored). Edit config.txt to change parameters."
	./main

CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -pthread
LDFLAGS := -pthread

BINARIES := main comando camion drone artilleria

all: $(BINARIES)

main: src/main.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

comando: src/comando.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

camion: src/camion.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

drone: src/drone.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

artilleria: src/artilleria.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: all
	./main

clean:
	rm -f $(BINARIES) *.o

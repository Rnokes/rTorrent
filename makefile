C=gcc
CFLAGS=-g -I. -Werror -Wall -pthread

all: rTorrent

rTorrent: rTorrent.o
	$(CC) $(CFLAGS) -o $@ $<
o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f *.o rTorrent

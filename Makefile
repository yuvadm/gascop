CFLAGS=-O2 -g -Wall -W `pkg-config --cflags librtlsdr`
LIBS=`pkg-config --libs librtlsdr` -lpthread -lm
CC=gcc
PROGNAME=dumposcag

all: dumpocsag

%.o: %.c
	$(CC) $(CFLAGS) -c $<

dump1090: dumpocsag.o
	$(CC) -g -o dumpocsag dumpocsag.o $(LIBS)

clean:
	rm -f *.o dumpocsag

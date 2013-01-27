CFLAGS=-O2 -g -Wall -W `pkg-config --cflags librtlsdr`
LIBS=`pkg-config --libs librtlsdr` -lpthread
CC=gcc
PROGNAME=gascop

all: gascop

%.o: %.c
	$(CC) $(CFLAGS) -c $<

gascop: gascop.o
	$(CC) -g -o gascop gascop.o $(LIBS)

clean:
	rm -f *.o gascop

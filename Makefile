CFLAGS = -Wall

all: powermate-mpd

powermate-mpd: powermate-mpd.o
	$(CC) powermate-mpd.o -o powermate-mpd -lmpdclient

clean:
	rm -f *.o powermate-mpd 

%.0:	%.c
	$(CC) -c $< -o $@ 

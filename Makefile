
CFLAGS=-std=gnu99
LDFLAGS=-lpthread -lm

viscaptz: main.o panasonicptz.o motorptz.o
	${CC} ${CFLAGS} ${LDFLAGS} main.o panasonicptz.o motorptz.o -lcurl -g -O0 -o viscaptz

clean:
	rm *.o viscaptz

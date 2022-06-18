
CFLAGS=-std=gnu99 -I./motorcontrol

# If not using hardware, remove -lbcm2835
LDFLAGS=-lpthread -lm -lbcm2835

viscaptz: main.o panasonicptz.o motorptz.o
	${CC} ${CFLAGS} main.o panasonicptz.o motorptz.o -lcurl -g -O0 ${LDFLAGS} -o viscaptz

clean:
	rm *.o viscaptz

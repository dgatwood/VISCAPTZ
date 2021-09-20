
viscaptz: main.o panasonicptz.o
	${CC} ${LDFLAGS} main.o panasonicptz.o -lcurl -g -O0 -o viscaptz

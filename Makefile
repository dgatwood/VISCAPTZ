
viscaptz: main.o panasonicptz.o motorptz.o
	${CC} ${LDFLAGS} main.o panasonicptz.o motorptz.o -lcurl -g -O0 -o viscaptz

clean:
	rm *.o viscaptz

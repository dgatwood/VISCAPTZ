
CFLAGS=-std=gnu99 -I./motorcontrol/lib/Config -I./motorcontrol/lib/MotorDriver -I./motorcontrol/lib/PCA9685 -Wall -Wno-unknown-pragmas -g -O0

# If not using hardware, remove -lbcm2835
LDFLAGS=-lpthread -lm
# LDFLAGS=-lpthread -lm -lbcm2835 -Lmotorcontrol -lmotorcontrol

%.o: %.c *.h
	${CC} -c ${CFLAGS} -o $@ $<

LINUX_TARGETS=
# LINUX_TARGETS=motorcontrol/libmotorcontrol.so

viscaptz: main.o configurator.o panasonicptz.o motorptz.o ${LINUX_TARGETS} *.h
	${CC} ${CFLAGS} main.o configurator.o panasonicptz.o motorptz.o -lcurl -g -O0 ${LDFLAGS} -o viscaptz

motorcontrol/libmotorcontrol.so:
	cd motorcontrol ; make libmotorcontrol.so ; sudo make install

clean:
	rm *.o viscaptz
	cd motorcontrol ; make clean

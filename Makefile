
UNAME := $(shell uname)

USE_OBS=1

CFLAGS=-std=gnu99 -I./motorcontrol/lib/Config -I./motorcontrol/lib/MotorDriver -I./motorcontrol/lib/PCA9685 -Wall -Wno-unknown-pragmas -g -O0

# If not using hardware, remove -lbcm2835
ifeq ($(UNAME), Linux)
LDFLAGS=-lpthread -lm -lbcm2835 -Lmotorcontrol -lmotorcontrol
else
LDFLAGS=-lpthread -lm
endif

ifeq ($(USE_OBS), 1)
ifeq ($(UNAME), Darwin)
# V8 binaries from brew are x86-64, so we build everything else that way.
CFLAGS+=-arch x86_64
endif
LDFLAGS+=-lgettally
endif

%.o: %.c *.h
	${CC} -c ${CFLAGS} -o $@ $<

ifeq ($(UNAME), Linux)
LINUX_TARGETS=motorcontrol/libmotorcontrol.so
else
LINUX_TARGETS=
endif

viscaptz: main.o obs_tally.o tricaster_tally.o configurator.o panasonicptz.o motorptz.o ${LINUX_TARGETS} *.h
	${CC} ${CFLAGS} main.o obs_tally.o tricaster_tally.o configurator.o panasonicptz.o motorptz.o -lcurl -g -O0 ${LDFLAGS} -o viscaptz

motorcontrol/libmotorcontrol.so:
	cd motorcontrol ; make libmotorcontrol.so ; sudo make install

clean:
	rm *.o viscaptz
	cd motorcontrol ; make clean

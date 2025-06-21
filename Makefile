### The tally source (the source of truth) can come from the Panasonic
### camera if you are controlling the tallly over NDI, or from a Tricaster
### or OBS switcher if you aren't.  Or, if you want to control the tally
### light exclusively over VISCA, you can specify a VISCA (fake) tally
### source that just stores the last received tally state in RAM.
###
### Tally source flags are controlled below.  These options are mutually
### exclusive.  If all are zero, this software treats VISCA requests as
### the source of truth.

# Use a Panasonic camera as the tally source.
USE_PANASONIC_TALLY_SOURCE=0

# Use OBS as the tally source.
USE_OBS_TALLY_SOURCE=0

# Use a Tricaster as the tally source.
USE_TRICASTER_TALLY_SOURCE=1

# Use motor controller (for directly driving motors) - requires Raspberry Pi/WiringPi.
# For Bescor, set to 0.
USE_MOTOR_DRIVER=0

### There are no user-configurable values below this line.  ###

UNAME := $(shell uname)

CFLAGS+=-std=gnu99 -Wall -Wno-unknown-pragmas -g -O0 -fno-omit-frame-pointer

ifeq ($(USE_MOTOR_DRIVER), 1)
CFLAGS+=-I./motorcontrol/lib/Config -I./motorcontrol/lib/MotorDriver -I./motorcontrol/lib/PCA9685 -DENABLE_PCA9685=1
else
CFLAGS+=-DENABLE_PCA9685=0
endif

# If not using hardware, remove -lbcm2835
LDFLAGS+=-lpthread -lm -lcrypto -lxml2

ifeq ($(UNAME), Linux)
CFLAGS+=-I/usr/include/libxml2/
ifeq ($(USE_MOTOR_DRIVER), 1)
LDFLAGS+=-lbcm2835 -Lmotorcontrol -lmotorcontrol
endif
endif

ifeq ($(UNAME), Darwin)
CFLAGS+=-I/opt/homebrew/Cellar/openssl@3/3.4.1/include/
LDFLAGS+=-L/opt/homebrew/Cellar/openssl@3/3.4.1/lib/
endif

ifeq ($(USE_OBS_TALLY_SOURCE), 1)
ifeq ($(UNAME), Darwin)
# V8 binaries from brew are x86-64, so we build everything else that way.
CFLAGS+=-arch x86_64
endif
CFLAGS+=-DUSE_OBS_TALLY_SOURCE=1
LDFLAGS+=-lgettally
else
  ifeq ($(USE_PANASONIC_TALLY_SOURCE), 1)
    CFLAGS+=-DUSE_PANASONIC_TALLY_SOURCE=1
  else
    ifeq ($(USE_TRICASTER_TALLY_SOURCE), 1)
      CFLAGS+=-DUSE_TRICASTER_TALLY_SOURCE=1
    else
      CFLAGS+=-DUSE_VISCA_TALLY_SOURCE=1
    endif
  endif
endif

%.o: %.c *.h
	${CC} -c ${CFLAGS} -o $@ $<

ifeq ($(UNAME), Linux)
ifeq ($(USE_MOTOR_DRIVER), 1)
LINUX_TARGETS=motorcontrol/libmotorcontrol.so
endif
else
LINUX_TARGETS=
endif

viscaptz: main.o obs_tally.o tricaster_tally.o configurator.o panasonic_shared.o panasonicptz.o p2protocol.o motorptz.o ${LINUX_TARGETS} *.h
	${CC} ${CFLAGS} main.o obs_tally.o tricaster_tally.o configurator.o panasonic_shared.o panasonicptz.o p2protocol.o motorptz.o -lcurl -g -O0 ${LDFLAGS} -o viscaptz

motorcontrol/libmotorcontrol.so:
	cd motorcontrol ; make libmotorcontrol.so ; sudo make install

clean:
	rm *.o viscaptz
	cd motorcontrol ; make clean

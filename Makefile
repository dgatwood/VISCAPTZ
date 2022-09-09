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
USE_PANASONIC_TALLY_SOURCE=1

# Use OBS as the tally source.
USE_OBS_TALLY_SOURCE=0

# Use a Tricaster as the tally source.
USE_TRICASTER_TALLY_SOURCE=0


### There are no user-configurable values below this line.  ###

UNAME := $(shell uname)

CFLAGS+=-std=gnu99 -I./motorcontrol/lib/Config -I./motorcontrol/lib/MotorDriver -I./motorcontrol/lib/PCA9685 -Wall -Wno-unknown-pragmas -g -O0 -fno-omit-frame-pointer

# If not using hardware, remove -lbcm2835
ifeq ($(UNAME), Linux)
LDFLAGS+=-lpthread -lm -lbcm2835 -Lmotorcontrol -lmotorcontrol
else
LDFLAGS+=-lpthread -lm
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

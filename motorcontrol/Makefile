DIR_OBJ = ./lib
DIR_BIN = ./objs
DIR_Config = ./lib/Config
DIR_MotorDriver = ./lib/MotorDriver
DIR_PCA9685 = ./lib/PCA9685
DIR_Examples = ./examples

OBJ_C = $(wildcard ${DIR_OBJ}/*.c ${DIR_Examples}/*.c ${DIR_Config}/*.c ${DIR_MotorDriver}/*.c ${DIR_PCA9685}/*.c )
OBJ_O = $(patsubst %.c,${DIR_BIN}/%.o,$(notdir ${OBJ_C}))

TARGET = libmotorcontrol.so
#BIN_TARGET = ${DIR_BIN}/${TARGET}

CC = gcc

DEBUG = -g -O0 -Wall
CFLAGS += $(DEBUG)

# Warning: USE_BCM2835_LIB appears to cause hangs on RPi 4.
# USELIB = USE_BCM2835_LIB

USELIB = USE_WIRINGPI_LIB

# USELIB = USE_DEV_LIB

DEBUG = -D $(USELIB) 
ifeq ($(USELIB), USE_BCM2835_LIB)
    LIB = -lbcm2835 -lm 
else ifeq ($(USELIB), USE_WIRINGPI_LIB)
    LIB = -lwiringPi -lm 

endif

${TARGET}:${OBJ_O}
	$(CC) -shared $(CFLAGS) $(OBJ_O) -o $@ $(LIB) -lm

${DIR_BIN}/%.o : $(DIR_Examples)/%.c
	mkdir -p $(DIR_BIN)
	$(CC) $(CFLAGS) -c  $< -o $@ $(LIB) -I $(DIR_OBJ) -I $(DIR_Config) -I $(DIR_MotorDriver) -I $(DIR_PCA9685)

${DIR_BIN}/%.o : $(DIR_OBJ)/%.c
	mkdir -p $(DIR_BIN)
	$(CC) $(CFLAGS) -c  $< -o $@ $(LIB) -I $(DIR_Config)
    
${DIR_BIN}/%.o : $(DIR_Config)/%.c
	mkdir -p $(DIR_BIN)
	$(CC) $(CFLAGS) -c  $< -o $@ $(LIB)

${DIR_BIN}/%.o : $(DIR_PCA9685)/%.c
	mkdir -p $(DIR_BIN)
	$(CC) $(CFLAGS) -c  $< -o $@ $(LIB) -I $(DIR_Config)

${DIR_BIN}/%.o : $(DIR_MotorDriver)/%.c
	mkdir -p $(DIR_BIN)
	$(CC) $(CFLAGS) -c  $< -o $@ $(LIB) -I $(DIR_Config) -I $(DIR_PCA9685)

install : ${TARGET}
	cp ${TARGET} /usr/local/lib/ ; ldconfig

clean :
	rm -f $(DIR_BIN)/*.* 
	rm -f $(TARGET) 

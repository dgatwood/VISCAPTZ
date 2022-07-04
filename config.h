#ifndef __CONFIG_H__
#define __CONFIG_H__

#ifndef __CONSTANTS_H__
#error Must include constants.h first.
#endif  // __CONSTANTS_H__

// ---------------------------------------------------------------------
// User-tuneable parameters
// ---------------------------------------------------------------------

#define CAMERA_IP "192.168.100.13"

#define CONFIG_FILE_PATH "/home/pi/viscaptz.conf"

#define EXPERIMENTAL_TIME_PROGRESS 1
#define USE_PANASONIC_PTZ 1
#define PANASONIC_PTZ_ZOOM_ONLY 1
#define USE_MOTOR_PAN_AND_TILT 1
#define ENABLE_HARDWARE 0
#define USE_CANBUS 1
#define USE_FAKE_PTZ 0

// For CANBus-based encoders.
#define panCANBusID 1
#define tiltCANBusID 2

// The minimum speed used for panning or tilting programmatically
// when we don't have calibration data.
#define MIN_PAN_TILT_SPEED 150

// Set to 1 if recall moves the motors in the wrong direction.
#define INVERT_PAN_AXIS 0
// #define INVERT_TILT_AXIS 1
#define INVERT_TILT_AXIS 0

// For RS485 encoders.
#define SERIAL_DEV_FILE_FOR_TILT "/dev/char/serial/uart0"
#define SERIAL_DEV_FILE_FOR_PAN "/dev/char/serial/uart1"

#endif  // __CONFIG_H__

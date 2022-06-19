#ifndef __CONFIG_H__
#define __CONFIG_H__

#ifndef __CONSTANTS_H__
#error Must include constants.h first.
#endif  // __CONSTANTS_H__

// ---------------------------------------------------------------------
// User-tuneable parameters
// ---------------------------------------------------------------------

#define USE_PANASONIC_PTZ 1
#define PANASONIC_PTZ_ZOOM_ONLY 1
#define USE_MOTOR_PAN_AND_TILT 1
#define ENABLE_HARDWARE 1
#define USE_CANBUS 1
#define USE_FAKE_PTZ 0

// For CANBus-based encoders.
#define panCANBusID 1
#define tiltCANBusID 2

// For RS485 encoders.
#define SERIAL_DEV_FILE_FOR_TILT "/dev/char/serial/uart0"
#define SERIAL_DEV_FILE_FOR_PAN "/dev/char/serial/uart1"

#endif  // __CONFIG_H__

#ifndef __CONFIG_H__
#define __CONFIG_H__

#ifndef __CONSTANTS_H__
#error Must include constants.h first.
#endif  // __CONSTANTS_H__

// ---------------------------------------------------------------------
// User-tuneable parameters
// ---------------------------------------------------------------------

#pragma mark - Local path configuration

// If not defined, uses ~/.viscaptz.conf
// #define CONFIG_FILE_PATH "/etc/viscaptz.conf"


#pragma mark - Tally master source

// The tally source (the source of truth) can come from the Panasonic
// camera if you are controlling the tallly over NDI, or from a Tricaster
// or OBS switcher if you aren't.  Or, if you want to control the tally
// light exclusively over VISCA, you can specify a VISCA (fake) tally
// source that just stores the last received tally state in RAM.
//
// IMPORTANT: Do not enable more than one tally source.

/**
 * Use a Panasonic camera as the tally source.
 */
#define USE_PANASONIC_TALLY_SOURCE 0

/** Use OBS as the tally source.  Not implemented. */
#define USE_TRICASTER_TALLY_SOURCE 1

/** Use a Tricaster as the tally source.  Not implemented. */
#define USE_OBS_TALLY_SOURCE 0

/**
 * Use this software as the source of truth, rather than monitoring
 * any external device.  Enabled if no other tally source is enabled.
 */
#define USE_VISCA_TALLY_SOURCE \
    !(USE_PANASONIC_TALLY_SOURCE || USE_TRICASTER_TALLY_SOURCE || \
      USE_OBS_TALLY_SOURCE)


#pragma mark - Motor configuration

/** 1 if using a Panasonic camera for zoom or PTZ, else 0. */
#define USE_PANASONIC_PTZ 1

/**
 * 1 if using a Panasonic camera for zoom only, but a different module for
 * pan and tilt.  0 if using Panasonic for pan, tilt, and zoom.
 *
 * If USE_PANASONIC_PTZ is 0, this setting is ignored.
 */
#define PANASONIC_PTZ_ZOOM_ONLY 1

/**
 * Enables motor control for pan and tilt, and external encoders for
 * pan and tilt position information.
 */
#define USE_MOTOR_PAN_AND_TILT 1

/** If 1, the motor and encoders are used.  If 0, the hardware is simulated. */
#define ENABLE_HARDWARE 1

/**
 * The minimum speed used for panning or tilting programmatically
 * when we don't have calibration data.
 */
#define MIN_PAN_TILT_SPEED 150


#pragma mark - Encoder configuration

/**
 * Chooses what type of encoder is connected.
 *
 * If 1, the encoder is expected to be on a CANBus connection.
 * If 0, the encoder is expected to be on an RS485 (Modbus) connection.
 *
 * This value is ignored if ENABLE_HARDWARE is 0.
 *
 * Note that RS485 encoders are entirely untested.  Remove this note if
 * that ever changes.
 */
#define USE_CANBUS 1

/** The identifiers for CANBus-based encoders. */
#define panCANBusID 1
#define tiltCANBusID 2

/** The serial ports for RS485 encoders (one port per encoder). */
#define SERIAL_DEV_FILE_FOR_TILT "/dev/char/serial/uart0"
#define SERIAL_DEV_FILE_FOR_PAN "/dev/char/serial/uart1"


#pragma mark - Experimental flags

/**
 * Set to 1 to use time-based progress, for proper smooth motion.
 * Set to 0 if you enjoy a bad experience, or if 1 is broken somehow.
 */
#define EXPERIMENTAL_TIME_PROGRESS 1


#pragma mark - Making builds simpler

#ifdef __APPLE__
#undef ENABLE_HARDWARE
#define ENABLE_HARDWARE 0
#endif

#endif  // __CONFIG_H__

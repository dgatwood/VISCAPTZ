#ifndef __CONFIG_H__
#define __CONFIG_H__

#ifndef __CONSTANTS_H__
#error Must include constants.h first.
#endif  // __CONSTANTS_H__

// ---------------------------------------------------------------------
// User-tuneable parameters
// ---------------------------------------------------------------------

#pragma mark - Local path configuration

#define CONFIG_FILE_PATH "/home/pi/viscaptz.conf"


#pragma mark - General camera info

/**
 * The IP address of an IP-based camera for getting and setting tally,
 * controlling and obtaining the status of zoom, etc.
 */
#define CAMERA_IP "192.168.100.13"

/**
 * The IP address of the Tricaster or OBS switcher for getting tally
 * information if either of those tally sources is enabled.
 */
#define TALLY_IP "192.168.100.1"

/**
 * The source/scene name that represents this camera, used to determine
 * the tally state when communicating with a Tricaster or OBS switcher.
 */
#define TALLY_SOURCE_NAME "NewTek 1"


#pragma mark - Tally master source

// The tally source (the source of truth) can come from the Panasonic
// camera if you are controlling the tallly over NDI, or from a Tricaster
// or OBS switcher if you aren't.  Or, if you want to control the tally
// light exclusively over VISCA, you can specify a VISCA (fake) tally
// source that just stores the last received tally state in RAM.

/** Use a Panasonic camera as the tally source. */
#define USE_PANASONIC_TALLY_SOURCE 1

/** Use OBS as the tally source.  Not implemented. */
#define USE_TRICASTER_TALLY_SOURCE 0

/** Use a Tricaster as the tally source.  Not implemented. */
#define USE_OBS_TALLY_SOURCE 0

/**
 * Use this software as the source of truth, rather than monitoring
 * any external device.
 */
#define USE_VISCA_TALLY_SOURCE 0

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
 * If 1, the encoder is expected to be on a CANBus connection.
 * If 0, the encoder is expected to be on an RS485 (Modbus) connection.
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

/** For early testing.  Do not use.  This should go away. */
#define USE_FAKE_PTZ 0


#pragma mark - Making builds simpler

#ifdef __APPLE__
#undef ENABLE_HARDWARE
#define ENABLE_HARDWARE 0
#endif

#endif  // __CONFIG_H__

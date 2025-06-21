#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#define __GNU_SOURCE  // Bizarrely required for asprintf in Linux.
#define __STDC_WANT_LIB_EXT2__ 1  // Bizarrely required for asprintf in Linux.

#ifndef USEC_PER_SEC
#define USEC_PER_SEC 1000000
#endif

// We use the same tally codes as NewTek and Marshall NDI cameras.
// Program mode is 5.  Preview mode is 6.
typedef enum {
  kTallyStateOff = 0,
  kTallyStateUnknown1 = 1,
  kTallyStateUnknown2 = 2,
  kTallyStateUnknown3 = 3,
  kTallyStateUnknown4 = 4,
  kTallyStateRed = 5,
  kTallyStateGreen = 6
} tallyState;

#define PAN_SCALE_VISCA   24
#define TILT_SCALE_VISCA  24
#define ZOOM_SCALE_VISCA  8

#define SCALE_CORE    1000

// Axis identifiers used in various spots throughout the code.
typedef enum {
  axis_identifier_pan = 0,
  axis_identifier_tilt = 1,
  axis_identifier_zoom = 2,
} axis_identifier_t;

#define NUM_AXES (axis_identifier_zoom + 1)


#include "config.h"

// Configuration keys and macros that are conditionally defined based
// on config.h.

#if USE_VISCA_TALLY_SOURCE
  #define GET_TALLY_STATE() VISCA_getTallyState()
#endif

#if USE_PANASONIC_PTZ
  extern const char *kCameraIPKey;
#endif

#if USE_TRICASTER_TALLY_SOURCE
  extern const char *kTricasterIPKey;
#endif

#if USE_OBS_TALLY_SOURCE
  extern const char *kOBSPasswordKey;
  extern const char *kOBSWebSocketURLKey;
#endif

#if (USE_OBS_TALLY_SOURCE || USE_TRICASTER_TALLY_SOURCE)
  #define TALLY_SOURCE_NAME_REQUIRED 1
  extern const char *kTallySourceName;
#endif

#if USE_MOTOR_PAN_AND_TILT
  extern const char *kMotorsAreSwappedKey;
#endif

// Untested, but designed for Briter's RS-485 (MODBUS) encoders.  Might work, or
// might need some additional fixes.
#define ENCODER_TYPE_SERIAL 0

// Tested with the Briter BRT38-COM4096D24-RT1.  Other CAN-based encoders may
// work adequately as long as they can handle enough turns and retain state
// across power cycling (or as long as you re-program your set positions every
// time you power it on).
#define ENCODER_TYPE_CANBUS 1

// Gravity 2-Channel 15Bit 0-10V ADC Module over I2C.
#define ENCODER_TYPE_GRAVITY_I2C 2

#endif  // __CONSTANTS_H__

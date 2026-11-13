#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

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
} tallyStateType;

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

#define kCalibrationDataPanKey "calibration_data_pan"
#define kCalibrationDataTiltKey "calibration_data_tilt"
#define kCalibrationDataZoomKey "calibration_data_zoom"

// Panasonic cameras report highly nonlinear zoom positions.  While this
// makes sense from a technical perspective, what we actually want is
// a linear number, where moving twice as many positions at a given speed
// takes roughly twice as long.
//
// These "internal" positions are raw values as provided by the camera.
// The scaled (linearized) values are stored in kZoomInLimitKey and
// kZoomOutLimitKey.
#define kZoomInInternalLimitKey "zoom_in_internal_limit"
#define kZoomOutInternalLimitKey "zoom_out_internal_limit"

#endif  // __CONSTANTS_H__

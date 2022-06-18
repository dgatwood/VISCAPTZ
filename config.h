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
#define USE_FAKE_PTZ 1

#endif  // __CONFIG_H__

#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

typedef enum {
  kDebugModePan = 0x1,
  kDebugModeTilt = 0x2,
  kDebugModeZoom = 0x4,
  kDebugScaling = 0x8,
  // kDebugLANC = 0x16,  // Unused in RasPi port.
} debugMode;

// We use the same tally codes as NewTek and Marshall NDI cameras.
// Program mode is 5, preview mode is 6.
enum {
  kTallyStateOff = 0,
  kTallyStateUnknown1 = 1,
  kTallyStateUnknown2 = 2,
  kTallyStateUnknown3 = 3,
  kTallyStateUnknown4 = 4,
  kTallyStateRed = 5,
  kTallyStateGreen = 6
};

#define PAN_SCALE_VISCA   24
#define TILT_SCALE_VISCA  24
#define ZOOM_SCALE_VISCA  8

#define SCALE_CORE    1000

// Not quite a constant, but used everywhere, and set once in main().
extern int debugPanAndTilt;

#include "config.h"

#endif  // __CONSTANTS_H__

#include "constants.h"

#if USE_FAKE_PTZ

#define MODULE_INIT() true

#define SET_IP_ADDR(address) true
#define GET_TALLY_STATE() debugGetTallyState()
#define SET_PAN_TILT_SPEED(panSpeed, tiltSpeed, isRaw) debugSetPanSpeed(panSpeed, tiltSpeed, isRaw)
#define SET_ZOOM_SPEED(speed, isRaw) debugSetZoomSpeed(speed, isRaw)

#define PAN_AND_TILT_POSITION_SUPPORTED false
#define ZOOM_POSITION_SUPPORTED false

#define PAN_TILT_SCALE_HARDWARE 1000
#define ZOOM_SCALE_HARDWARE 1000

#endif

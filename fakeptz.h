#include "constants.h"

#if USE_FAKE_PTZ

#define MODULE_INIT() true

#define SET_IP_ADDR(address) true
#define GET_TALLY_STATE() debugGetTallyState()
#define SET_PAN_SPEED(speed) debugSetPanSpeed(speed)
#define SET_TILT_SPEED(speed) debugSetTiltSpeed(speed)
#define SET_ZOOM_SPEED(speed) debugSetZoomSpeed(speed)

#define PAN_AND_TILT_POSITION_SUPPORTED false
#define ZOOM_POSITION_SUPPORTED false

#define PAN_SCALE_HARDWARE 1000
#define TILT_SCALE_HARDWARE 1000
#define ZOOM_SCALE_HARDWARE 1000

#endif

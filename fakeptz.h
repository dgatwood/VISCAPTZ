#define MODULE_INIT() true
#define SET_IP_ADDR(address) true
#define GET_TALLY_STATE() debugGetTallyState()
#define GET_PAN_POSITION() debugGetPanPosition()
#define GET_TILT_POSITION() debugGetTiltPosition()
#define GET_ZOOM_POSITION() debugGetZoomPosition()
#define SET_PAN_SPEED(speed) debugSetPanSpeed(speed)
#define SET_TILT_SPEED(speed) debugSetTiltSpeed(speed)
#define SET_ZOOM_SPEED(speed) debugSetZoomSpeed(speed)
#define SET_ZOOM_POSITION(position, maxSpeed) setZoomPositionIncrementally(position, maxSpeed)

#define SPEED_FOR_PAN(fromPosition, toPosition) 1.0
#define SPEED_FOR_TILT(fromPosition, toPosition) 1.0
#define SPEED_FOR_ZOOM(fromPosition, toPosition) 1.0

#define PAN_SPEED_SCALE(speedInt) (speedInt * 1.0)
#define TILT_SPEED_SCALE(speedInt) (speedInt * 1.0)
#define ZOOM_SPEED_SCALE(speedInt) (speedInt * 1.0)

#define PAN_AND_TILT_POSITION_SUPPORTED false
#define ZOOM_POSITION_SUPPORTED false


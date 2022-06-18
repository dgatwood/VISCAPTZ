
#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"

extern bool motorPanTiltPositionEnabled;

bool motorModuleInit(void);
bool motorSetIPAddress(char *address);
bool motorSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed);
bool motorGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition);
int motorGetTallyState(void);

int64_t motorPanSpeed(int64_t fromPosition, int64_t toPosition);
int64_t motorTiltSpeed(int64_t fromPosition, int64_t toPosition);

#if USE_MOTOR_PAN_AND_TILT

    #define GET_PAN_TILT_POSITION(panPositionRef, tiltPositionRef) \
        motorGetPanTiltPosition(panPositionRef, tiltPositionRef)
    #define SET_PAN_TILT_SPEED(panSpeed, tiltSpeed) \
        motorSetPanTiltSpeed(panSpeed, tiltSpeed)
    #define SET_PAN_TILT_POSITION(panPosition, panSpeed, tiltPosition, tiltSpeed) \
        (setAxisPositionIncrementally(axis_identifier_pan, panPosition, panSpeed) && \
        setAxisPositionIncrementally(axis_identifier_tilt, tiltPosition, tiltSpeed))
    #define PAN_SPEED_SCALE(speedInt) (speedInt * 1.0)
    #define TILT_SPEED_SCALE(speedInt) (speedInt * 1.0)

    #define PAN_AND_TILT_POSITION_SUPPORTED true

    // No idea what these values should be.
    #define PAN_RANGE 1000
    #define TILT_RANGE 1000

    #define PAN_TILT_SCALE_HARDWARE 100

#endif

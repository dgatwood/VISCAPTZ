
#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"

void reassign_encoder_device_id(int oldCANBusID, int newCANBusID);

extern bool motorPanTiltPositionEnabled;

bool motorModuleInit(void);
void motorModuleCalibrate(void);
bool motorSetIPAddress(char *address);
bool motorSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed, bool isRaw);
bool motorGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition);
int motorGetTallyState(void);

int64_t motorPanSpeed(int64_t fromPosition, int64_t toPosition);
int64_t motorTiltSpeed(int64_t fromPosition, int64_t toPosition);

#if USE_MOTOR_PAN_AND_TILT

    #define GET_PAN_TILT_POSITION(panPositionRef, tiltPositionRef) \
        motorGetPanTiltPosition(panPositionRef, tiltPositionRef)
    #define SET_PAN_TILT_SPEED(panSpeed, tiltSpeed, isRaw) \
        motorSetPanTiltSpeed(panSpeed, tiltSpeed, isRaw)
    #define SET_PAN_TILT_POSITION(panPosition, panSpeed, tiltPosition, tiltSpeed) \
        (setAxisPositionIncrementally(axis_identifier_pan, panPosition, panSpeed) && \
        setAxisPositionIncrementally(axis_identifier_tilt, tiltPosition, tiltSpeed))
    #define PAN_SPEED_SCALE(speedInt) (speedInt * 1.0)
    #define TILT_SPEED_SCALE(speedInt) (speedInt * 1.0)

    #define PAN_AND_TILT_POSITION_SUPPORTED true

    #define PAN_TILT_SCALE_HARDWARE 100

#endif

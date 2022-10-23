
#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"

/** Reassigns the CANBus encoder at oldCANBusID so that it will be at newCANBusID. */
void reassign_encoder_device_id(int oldCANBusID, int newCANBusID);

/**
 * Initializes the motor control module (and encoders) and starts motor
 * control and encoder position monitor thread.
 */
bool motorModuleInit(void);

/** Reinitializes the motor control module (and encoders) after calibration. */
bool motorModuleReload(void);

/**
 * Performs a motor module calibration cycle, computing the number of positions
 * per second at each motor speed in each axis, then computing each of those
 * values relative to the value at the maximum speed.
 *
 * This function uses calibrationDataForMoveAlongAxis to repeatedly move
 * back and forth at various speeds.
 */
void motorModuleCalibrate(void);

/**
 * Sets the pan and tilt speeds of the motor module.
 *
 * @param panSpeed  The pan speed.
 * @param tiltSpeed The tilt speed.
 * @param isRaw     If true, the speed values are in hardware scale
 *                  (from -100 to 100).  If false, the speed values are
 *                  in core scale (from -1000 to 1000).
 */
bool motorSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed, bool isRaw);

/**
 * Gets the current pan and tilt position from the encoders.  The scale is
 * arbitrary and depends on the encoder and gearing.
 *
 * @param panPosition Storage for the position of the pan encoder (output parameter)
 *                    or NULL.
 * @param panPosition Storage for the position of the pan encoder (output parameter)
 *                    or NULL.
 */
bool motorGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition);

/**
 * Returns the number of encoder positions per second that the pan axis moves
 * when operating at its slowest speed.  Returns 0 if no calibration data is
 * available.
 */
int64_t motorMinimumPanPositionsPerSecond(void);

/**
 * Returns the number of encoder positions per second that the tilt axis moves
 * when operating at its slowest speed.  Returns 0 if no calibration data is
 * available.
 */
int64_t motorMinimumTiltPositionsPerSecond(void);

/**
 * Returns the number of encoder positions per second that the pan axis moves
 * when operating at its fastest non-stalled speed (i.e. the slowest speed
 * at which the encoder moves at nonzero positions per second).  Returns 0 if
 * no calibration data is available.
 */
int64_t motorMaximumPanPositionsPerSecond(void);

/**
 * Returns the number of encoder positions per second that the tilt axis moves
 * when operating at its fastest speed.  Returns 0 if no calibration data is
 * available.
 */
int64_t motorMaximumTiltPositionsPerSecond(void);


// If motor pan and tilt are enabled, map the standard motion and position macros
// to functions in this module.
#if USE_MOTOR_PAN_AND_TILT

    #define GET_PAN_TILT_POSITION(panPositionRef, tiltPositionRef) \
        motorGetPanTiltPosition(panPositionRef, tiltPositionRef)
    #define SET_PAN_TILT_SPEED(panSpeed, tiltSpeed, isRaw) \
        motorSetPanTiltSpeed(panSpeed, tiltSpeed, isRaw)
    #define SET_PAN_TILT_POSITION(panPosition, panSpeed, tiltPosition, tiltSpeed, \
                                  panDuration, tiltDuration, panStartTime, tiltStartTime) \
        (setAxisPositionIncrementally(axis_identifier_pan, panPosition, panSpeed, panDuration, panStartTime) && \
        setAxisPositionIncrementally(axis_identifier_tilt, tiltPosition, tiltSpeed, tiltDuration, tiltStartTime))
    #define PAN_SPEED_SCALE(speedInt) (speedInt * 1.0)
    #define TILT_SPEED_SCALE(speedInt) (speedInt * 1.0)

    #define PAN_AND_TILT_POSITION_SUPPORTED true

    #define PAN_TILT_SCALE_HARDWARE 100

    #define MIN_PAN_POSITIONS_PER_SECOND() motorMinimumPanPositionsPerSecond();
    #define MIN_TILT_POSITIONS_PER_SECOND() motorMinimumTiltPositionsPerSecond();
    #define MAX_PAN_POSITIONS_PER_SECOND() motorMaximumPanPositionsPerSecond();
    #define MAX_TILT_POSITIONS_PER_SECOND() motorMaximumTiltPositionsPerSecond();
#endif

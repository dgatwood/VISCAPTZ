#include <sys/types.h>

#include "constants.h"

#pragma mark - Base implementation functions, callable from modules.

// True if we are in calibration mode.
bool gCalibrationMode;

/**
 * Returns an array where position 0 is the number of positions
 * moved in one second at min_speed, position 1 is at min_speed + 1,
 * and so on.  The caller is responsible for freeing the array.
 */
int64_t *calibrationDataForMoveAlongAxis(axis_identifier_t axis,
                                         int64_t startPosition,
                                         int64_t endPosition,
                                         int32_t min_speed,
                                         int32_t max_speed);

/** Reads the calibration data from the configurator. */
int64_t *readCalibrationDataForAxis(axis_identifier_t axis,
                                    int *length);

/** Writes the calibration data to the configurator. */
bool writeCalibrationDataForAxis(axis_identifier_t axis,
                                 int64_t *calibrationData,
                                 int length);

bool panMotorReversed(void);
bool tiltMotorReversed(void);
bool panEncoderReversed(void);
bool tiltEncoderReversed(void);

int64_t leftPanLimit(void);
int64_t rightPanLimit(void);
int64_t topTiltLimit(void);
int64_t bottomTiltLimit(void);

int scaleSpeed(int speed, int fromScale, int toScale);

int debugGetTallyState(void);
bool debugSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed, bool isRaw);
bool debugSetZoomSpeed(int64_t speed);
bool debugGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition);
int64_t debugGetZoomPosition(void);

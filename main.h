#include <sys/types.h>

#include "constants.h"

#pragma mark - Base implementation functions, callable from modules.

// True if we are in calibration mode.
extern bool gCalibrationMode;
extern bool gCalibrationModeQuick;
extern bool gRecenter;

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
int64_t zoomInLimit(void);
int64_t zoomOutLimit(void);
int64_t zoomEncoderReversed(void);
int64_t setZoomInLimit(int64_t limit);
int64_t setZoomOutLimit(int64_t limit);
int64_t setZoomEncoderReversed(bool isReversed);

// Scales a speed from a scale of 0..fromScale to 0..toScale.
//
// If scaleData is NULL, 0 is 0, and all other input values map onto
// equally sized groups of numbers on the output size or input groups onto
// single output values, depending on direction.
//
// If scaleData is non-NULL, it is assumed to be a set of values that
// are equal to the raw scale value for that motor speed divided by
// the raw scale value for the fastest motor position times 1,000.
//
// Thus, each value represents the core scale value that most closely
// approximates that speed in the target scale.  Any zero-speed values
// are skipped and replaced by the first nonzero value.  This isn't
// exactly right mathematically, but it is as close as is physically
// possible given motors' tendency to stall out at low speeds.
// If fromScale is not the core scale, the value is first converted
// to that scale.
int scaleSpeed(int speed, int fromScale, int toScale, int32_t *scaleData);

int debugGetTallyState(void);
bool debugSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed, bool isRaw);
bool debugSetZoomSpeed(int64_t speed);
bool debugGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition);
int64_t debugGetZoomPosition(void);

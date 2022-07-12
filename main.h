#include <sys/types.h>

#include "constants.h"

#pragma mark - Base implementation functions, callable from modules.

// True if we are in calibration mode.
extern bool gCalibrationMode;
extern bool gCalibrationModeQuick;
extern bool gRecenter;

/** Returns a string providing the name of a tally state. */
const char *tallyStateName(tallyState);

/** Sets the tally light to red (if possible). */
bool setTallyRed(void);

/** Sets the tally light to green (if possible). */
bool setTallyGreen(void);

/** Sets the tally light to off (if possible). */
bool setTallyOff(void);

/**
 * Returns an array where position 0 is the number of positions
 * moved in one second at minSpeed, position 1 is at minSpeed + 1,
 * and so on.  The caller is responsible for freeing the array.
 */
int64_t *calibrationDataForMoveAlongAxis(axis_identifier_t axis,
                                         int64_t startPosition,
                                         int64_t endPosition,
                                         int32_t minSpeed,
                                         int32_t maxSpeed);

/** Reads the calibration data from the configurator. */
int64_t *readCalibrationDataForAxis(axis_identifier_t axis,
                                    int *maxSpeed);

/** Writes the calibration data to the configurator. */
bool writeCalibrationDataForAxis(axis_identifier_t axis,
                                 int64_t *calibrationData,
                                 int length);

/**
 * Converts an array of raw scale values into a scaled array.
 *
 * Each of the input speed values represents the number of
 * encoder positions that the motor on a given axis moves
 * in one second.
 *
 * Each of the output speed values represents roughly the
 * core speed that corresponds to that native (physical)
 * speed value, computed by dividing the input value by
 * the largest input value and multiplying by 1,000.
 *
 * This adds a small episilon to the largest value so that
 * core values slightly below 1,000 will map on to the
 * maximum value.
 *
 * This function returns an array allocated with malloc.
 * it must be freed by the caller when no longer needed.
 *
 * @param speedValues An array of raw data from a previous
 *                    `calibrationDataForMoveAlongAxis` call.
 * @param maxSpeed    The maximum speed in the array (i.e.
 *                    one more than the number of items in
 *                    the array --- typically the value of
 *                    a *_SCALE_HARDWARE constant.
 */
int32_t *convertSpeedValues(int64_t *speedValues, int maxSpeed);

/**
 * Returns the first nonzero value in the provided calibration data, or 0 if data is nil
 * or all zeroes.
 */
int64_t minimumPositionsPerSecondForData(int64_t *calibrationData, int maximumSpeed);

bool panMotorReversed(void);
bool tiltMotorReversed(void);
bool panEncoderReversed(void);
bool tiltEncoderReversed(void);
int64_t zoomEncoderReversed(void);
int64_t setZoomEncoderReversed(bool isReversed);

int64_t leftPanLimit(void);
int64_t rightPanLimit(void);

int64_t topTiltLimit(void);
int64_t bottomTiltLimit(void);

int64_t zoomInLimit(void);
int64_t zoomOutLimit(void);
int64_t setZoomInLimit(int64_t limit);
int64_t setZoomOutLimit(int64_t limit);

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

// Returns the current tally state as cached in main.c.  Used if there is
// no other tally source defined.
tallyState VISCA_getTallySource(void);

int debugGetTallyState(void);
bool debugSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed, bool isRaw);
bool debugSetZoomSpeed(int64_t speed);
bool debugGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition);
int64_t debugGetZoomPosition(void);

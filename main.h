#include <sys/types.h>

#include "constants.h"

#pragma mark - Base implementation functions, callable from modules.

/** True when in calibration mode. */
extern bool gCalibrationMode;

/** True if we are recalibrating. */
extern bool gCalibrationModeQuick;

/** True if we are recentering the encoders. */
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
 * Performs calibration for motion on a given axis, computing a
 * mapping table that tells how many encoder positions the axis
 * moves in one second at each of the (raw hardware) speed values
 * from minSpeed through maxSpeed.
 *
 * This takes a significant amount of time, and should be performed
 * only when the hardware changes.
 *
 * @param axis          
 * @param startPosition The leftmost or topmost position or most zoomed
 *                      out position used during calibration.  The
 *                      calibration code moves the axis back and forth
 *                      between this position and endPosition.
 * @param endPosition   The rightmost or bottommost or most zoomed in
 *                      position used during calibration.
 * @param minSpeed      The minimum (hardware-scale speed that should
 *                      be used during calibration.
 * @param maxSpeed      The maximum (hardware-scale speed that should
 *                      be used during calibration.
 * @param pollingIsSlow True for sources where checking the motor position
 *                      is slow.  This allows for longer durations per
 *                      measurement to get a more precise value.
 *
 * @result Returns an array where position 0 is the number of positions
 *         moved in one second at minSpeed, position 1 is at minSpeed + 1,
 *         and so on.  The caller is responsible for freeing the array.
 */
int64_t *calibrationDataForMoveAlongAxis(axis_identifier_t axis,
                                         int64_t startPosition,
                                         int64_t endPosition,
                                         int32_t minSpeed,
                                         int32_t maxSpeed,
                                         bool pollingIsSlow);

/**
 * Reads the calibration data for the specified axis from the
 * configuration file and returns it as an array.  The caller is
 * responsible for freeing the resulting array.
 */
int64_t *readCalibrationDataForAxis(axis_identifier_t axis,
                                    int *maxSpeed);

/**
 * Writes the calibration data for the specified axis to the
 * configuration file.  Returns true if the operation was
 * successful, else false.
 */
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

/** True if the pan motor moves to the right when sent positive speed values. */
bool panMotorReversed(void);

/** True if the tilt motor moves to down when sent positive speed values. */
bool tiltMotorReversed(void);

/** True if the pan encoder values increase when moving to the right. */
bool panEncoderReversed(void);

/** True if the tilt encoder values increase when moving down. */
bool tiltEncoderReversed(void);

/** True if the zoom position value increases when zooming out. */
int64_t zoomEncoderReversed(void);

/**
 * Sets the value returned by zoomEncoderReversed.
 *
 * @param isReversed True if the zoom position value increases
 *                   when zooming out, else false.
 */
int64_t setZoomEncoderReversed(bool isReversed);

/** The leftmost position used during calibration. */
int64_t leftPanLimit(void);

/** The rightmost position used during calibration. */
int64_t rightPanLimit(void);

/** The topmost position used during calibration. */
int64_t topTiltLimit(void);

/** The bottommost position used during calibration. */
int64_t bottomTiltLimit(void);

/** The maximally zoomed in position used during calibration. */
int64_t zoomInLimit(void);

/** The maximally zoomed out position used during calibration. */
int64_t zoomOutLimit(void);

/** Sets the position returned by zoomInLimit. */
int64_t setZoomInLimit(int64_t limit);

/** Sets the position returned by zoomOutLimit. */
int64_t setZoomOutLimit(int64_t limit);

/**
 * Scales a speed from a scale of 0..fromScale to 0..toScale.
 *
 * @param speed     The speed value in the original scale (0 to fromScale).
 * @param fromScale The current scale.
 * @param toScale   The target scale.
 *
 * If scaleData is NULL, 0 is 0, and all other input values map onto
 * equally sized groups of numbers on the output size or input groups onto
 * single output values, depending on direction.
 *
 * If scaleData is non-NULL, it is assumed to be a set of values that
 * are equal to the raw scale value for that motor speed divided by
 * the raw scale value for the fastest motor position times 1,000.
 *
 * Thus, each value represents the core scale value that most closely
 * approximates that speed in the target scale.  Any zero-speed values
 * are skipped and replaced by the first nonzero value.  This isn't
 * exactly right mathematically, but it is as close as is physically
 * possible given motors' tendency to stall out at low speeds.
 * If fromScale is not the core scale, the value is first converted
 * to that scale.
 */
int scaleSpeed(int speed, int fromScale, int toScale, int32_t *scaleData);

/**
 * Returns the current tally state as cached in main.c.  Used if there is
 * no other tally source defined.
 */
tallyState VISCA_getTallyState(void);

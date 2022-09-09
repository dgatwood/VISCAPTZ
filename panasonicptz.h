
#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"

/** Initializes the Panasonic pan/tilt/zoom module. */
bool panaModuleInit(void);

/** Reinitializes the Panasonic pan/tilt/zoom module after calibration. */
bool panaModuleReload(void);

/** Calibrates the Panasonic pan/tilt/zoom module. */
void panaModuleCalibrate(void);

/** Cleans up the Panasonic pan/tilt/zoom module. */
bool panaModuleTeardown(void);

/** Sets the camera IP address. */
bool panaSetIPAddress(char *address);

/** Sets the pan and tilt speed (for true PTZ cameras only). */
bool panaSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed);

/**
 * Sets the zoom speed.
 *
 * @param speed The zoom speed.
 * @param isRaw If true, the speed is in hardware scale (-49 to 49).
 *              If false, the speed is in core scale.
 */
bool panaSetZoomSpeed(int64_t speed, bool isRaw);

/** Gets the pan and tilt position (for true PTZ cameras only). */
bool panaGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition);

/** Gets the current zoom position. */
int64_t panaGetZoomPosition(void);

/** Gets the current zoom speed (for debugging). */
int64_t panaGetZoomSpeed(void);

/**
 * Moves the camera to the specified pan and tilt position at the specified speed
 * (for true PTZ cameras only).
 */
bool panaSetPanTiltPosition(int64_t panPosition, int64_t panSpeed,
                            int64_t tiltPosition, int64_t tiltSpeed);

/** Moves the camera to the specified zoom position at the specified speed.  */
bool panaSetZoomPosition(int64_t position, int64_t maxSpeed);

/** Gets the current tally state from the camera. */
int panaGetTallyState(void);

/** Changes the camera's tally light state. */
bool panaSetTallyState(int tallyState);

/** The number of pan positions moved in one second at the minimum speed. */
int64_t panaMinimumPanPositionsPerSecond(void);

/** The number of tilt positions moved in one second at the minimum speed. */
int64_t panaMinimumTiltPositionsPerSecond(void);

/** The number of zoom positions moved in one second at the minimum speed. */
int64_t panaMinimumZoomPositionsPerSecond(void);

/** The number of pan positions moved in one second at the maximum speed. */
int64_t panaMaximumPanPositionsPerSecond(void);

/** The number of tilt positions moved in one second at the maximum speed. */
int64_t panaMaximumTiltPositionsPerSecond(void);

/** The number of zoom positions moved in one second at the maximum speed. */
int64_t panaMaximumZoomPositionsPerSecond(void);

// If Panasonic zoom is enabled (and maybe pan and tilt), map the standard
// motion and position macros to functions in this module.
#if USE_PANASONIC_PTZ

    #define MODULE_INIT() panaModuleInit()
    #define SET_IP_ADDR(address) panaSetIPAddress(address);

    // If the Panasonic tally source is enabled, map the tally state getter
    // macro onto a function in this module.
    #if USE_PANASONIC_TALLY_SOURCE
        #define GET_TALLY_STATE() panaGetTallyState()
    #endif

    #define SET_TALLY_STATE(state) panaSetTallyState(state)
    #define GET_ZOOM_POSITION() panaGetZoomPosition()
    #define GET_ZOOM_SPEED() panaGetZoomSpeed()
    #define ZOOM_POSITION_SUPPORTED true
    #define SET_ZOOM_SPEED(speed, isRaw) panaSetZoomSpeed(speed, isRaw)

    // Possible future options: LANC max is 8 (officially 0-7 plus "stopped").
    #define ZOOM_SCALE_HARDWARE 49

    // Cameras without pan and tilt motors do not allow you to jump to a specific
    // zoom position.  Others don't provide speed control, so you may not want to
    // use the zoom command anyway.
    #if PANASONIC_PTZ_ZOOM_ONLY || PANASONIC_DISABLE_ZOOM_COMMAND
        #define SET_ZOOM_POSITION(position, maxSpeed, time) \
            setAxisPositionIncrementally(axis_identifier_zoom, position, maxSpeed, time)
    #else  // !(PANASONIC_PTZ_ZOOM_ONLY || PANASONIC_DISABLE_ZOOM_COMMAND)
        #define SET_ZOOM_POSITION(position, maxSpeed, time) \
            panaSetZoomPosition(axis_identifier_zoom, position, maxSpeed)
    #endif  // PANASONIC_PTZ_ZOOM_ONLY || PANASONIC_DISABLE_ZOOM_COMMAND

    #define MIN_ZOOM_POSITIONS_PER_SECOND() panaMinimumZoomPositionsPerSecond();
    #define MAX_ZOOM_POSITIONS_PER_SECOND() panaMaximumZoomPositionsPerSecond();

    // If pan and tilt are enabled (not zoom only), map the pan and tilt getter and
    // setter macros to functions in this module.
    #if !PANASONIC_PTZ_ZOOM_ONLY
        #define GET_PAN_TILT_POSITION(panPositionRef, tiltPositionRef) \
            panaGetPanTiltPosition(panPositionRef, tiltPositionRef)
        #define SET_PAN_TILT_SPEED(panSpeed, tiltSpeed, isRaw) \
            panaSetPanSpeed(panSpeed, tiltSpeed, isRaw)
        #define SET_PAN_TILT_POSITION(panPosition, panSpeed, tiltPosition, tiltSpeed, panTime, tiltTime) 
            panaSetPanTiltPosition(panPosition, panSpeed, tiltPosition, tiltSpeed)
        #define PAN_SPEED_SCALE(speedInt) (speedInt * 1.0)
        #define TILT_SPEED_SCALE(speedInt) (speedInt * 1.0)
        #define PAN_AND_TILT_POSITION_SUPPORTED true

        // Panasonic defines the range as 0 to 99, with 50 idle.  We remap this to
        // -49 to 49 and ignore the additional negative value (hardware value 0).
        #define PAN_TILT_SCALE_HARDWARE 49

        #define MIN_PAN_POSITIONS_PER_SECOND() panaMinimumPanPositionsPerSecond();
        #define MIN_TILT_POSITIONS_PER_SECOND() panaMinimumTiltPositionsPerSecond();
        #define MAX_PAN_POSITIONS_PER_SECOND() panaMaximumPanPositionsPerSecond();
        #define MAX_TILT_POSITIONS_PER_SECOND() panaMaximumTiltPositionsPerSecond();
    #endif

#endif

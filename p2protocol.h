
#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"

/** Initializes the Panasonic pan/tilt/zoom module. */
bool p2ModuleInit(void);

/** Starts the Panasonic pan/tilt/zoom module. */
bool p2ModuleStart(void);

/** Reinitializes the Panasonic pan/tilt/zoom module after calibration. */
bool p2ModuleReload(void);

/** Calibrates the Panasonic pan/tilt/zoom module. */
void p2ModuleCalibrate(void);

/** Cleans up the Panasonic pan/tilt/zoom module. */
bool p2ModuleTeardown(void);

/** Sets the camera IP address. */
bool p2SetIPAddress(char *address);

/**
 * Gets the maximum possible zoom range for the camera (based on calibration data).
 * Returns true if calibration data is available, else false.
 */
bool p2GetZoomRange(int64_t *min, int64_t *max);

/**
 * Sets the zoom speed.
 *
 * @param speed The zoom speed.
 * @param isRaw If true, the speed is in hardware scale (-49 to 49).
 *              If false, the speed is in core scale.
 */
bool p2SetZoomSpeed(int64_t speed, bool isRaw);

/** Gets the current zoom position. */
int64_t p2GetZoomPosition(void);

/** Gets the current zoom speed (for debugging). */
int64_t p2GetZoomSpeed(void);

/** Gets the current tally state from the camera. */
int p2GetTallyState(void);

/** Changes the camera's tally light state. */
bool p2SetTallyState(int tallyState);

/** The number of zoom positions moved in one second at the minimum speed. */
int64_t p2MinimumZoomPositionsPerSecond(void);

/** The number of zoom positions moved in one second at the maximum speed. */
int64_t p2MaximumZoomPositionsPerSecond(void);

// If Panasonic zoom is enabled, map the standard motion and position macros to functions in this module.
#if USE_PANASONIC_PTZ && ENABLE_P2_MODE

    #define MODULE_INIT() p2ModuleInit()
    #define SET_IP_ADDR(address) p2SetIPAddress(address);

    // If the Panasonic tally source is enabled, map the tally state getter
    // macro onto a function in this module.
    #if USE_PANASONIC_TALLY_SOURCE
        #define GET_TALLY_STATE() p2GetTallyState()
    #endif

    int64_t p2GetZoomPositionRaw(void);

    #define SET_TALLY_STATE(state) p2SetTallyState(state)
    #define GET_RAW_ZOOM_POSITION() p2GetZoomPositionRaw()
    #define GET_ZOOM_RANGE(min, max) p2GetZoomRange(min, max)  // uint64_t *
    #define GET_ZOOM_POSITION() p2GetZoomPosition()
    #define GET_ZOOM_SPEED() p2GetZoomSpeed()
    #define ZOOM_POSITION_SUPPORTED true
    #define SET_ZOOM_SPEED(speed, isRaw) p2SetZoomSpeed(speed, isRaw)

    #define ZOOM_SCALE_HARDWARE 8
    #define ZOOM_MIN_HARDWARE 1  // 1..8

    #define SET_ZOOM_POSITION(position, maxSpeed, time, startTime) \
        setAxisPositionIncrementally(axis_identifier_zoom, position, maxSpeed, time, startTime)

    #define MIN_ZOOM_POSITIONS_PER_SECOND() p2MinimumZoomPositionsPerSecond();
    #define MAX_ZOOM_POSITIONS_PER_SECOND() p2MaximumZoomPositionsPerSecond();
#endif

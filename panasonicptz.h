
#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"

bool panaModuleInit(void);
void panaModuleCalibrate(void);
bool panaModuleTeardown(void);
bool panaSetIPAddress(char *address);
bool panaSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed);
bool panaSetZoomSpeed(int64_t speed, bool isRaw);
bool panaGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition);
int64_t panaGetZoomPosition(void);
int64_t panaGetZoomSpeed(void);
bool panaSetPanTiltPosition(int64_t panPosition, int64_t panSpeed,
                            int64_t tiltPosition, int64_t tiltSpeed);
bool panaSetZoomPosition(int64_t position, int64_t maxSpeed);
int panaGetTallyState(void);
bool panaSetTallyState(int tallyState);

// Actual range on the AG-CX350.  Other cameras may vary.
#define ZOOM_RANGE 4720

#if USE_PANASONIC_PTZ

    #define MODULE_INIT() panaModuleInit()
    #define SET_IP_ADDR(address) panaSetIPAddress(address);

    #define GET_TALLY_STATE() panaGetTallyState()
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
        #define SET_ZOOM_POSITION(position, maxSpeed) \
            setAxisPositionIncrementally(axis_identifier_zoom, position, maxSpeed)
    #else  // !(PANASONIC_PTZ_ZOOM_ONLY || PANASONIC_DISABLE_ZOOM_COMMAND)
        #define SET_ZOOM_POSITION(position, maxSpeed) \
            panaSetZoomPosition(axis_identifier_zoom, position, maxSpeed)
    #endif  // PANASONIC_PTZ_ZOOM_ONLY || PANASONIC_DISABLE_ZOOM_COMMAND

    #if !PANASONIC_PTZ_ZOOM_ONLY
        #define GET_PAN_TILT_POSITION(panPositionRef, tiltPositionRef) \
            panaGetPanTiltPosition(panPositionRef, tiltPositionRef)
        #define SET_PAN_TILT_SPEED(panSpeed, tiltSpeed, isRaw) \
            panaSetPanSpeed(panSpeed, tiltSpeed, isRaw)
        #define SET_PAN_TILT_POSITION(panPosition, panSpeed, tiltPosition, tiltSpeed) 
            panaSetPanTiltPosition(panPosition, panSpeed, tiltPosition, tiltSpeed)
        #define PAN_SPEED_SCALE(speedInt) (speedInt * 1.0)
        #define TILT_SPEED_SCALE(speedInt) (speedInt * 1.0)
        #define PAN_AND_TILT_POSITION_SUPPORTED true

        // Panasonic defines the range as 0 to 99.
        #define PAN_TILT_SCALE_HARDWARE 49
    #endif

#endif

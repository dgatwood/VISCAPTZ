
#include <inttypes.h>
#include <stdbool.h>

#define MODULE_INIT() panaModuleInit()
#define SET_IP_ADDR(address) panaSetIPAddress(address);

#define GET_TALLY_STATE() panaGetTallyState()
#define GET_PAN_POSITION() panaGetPanPosition()
#define GET_TILT_POSITION() panaGetTiltPosition()
#define GET_ZOOM_POSITION() panaGetZoomPosition()
#define SET_PAN_SPEED(speed) panaSetPanSpeed(speed)
#define SET_TILT_SPEED(speed) panaSetTiltSpeed(speed)
#define SET_ZOOM_SPEED(speed) panaSetZoomSpeed(speed)
#define SET_ZOOM_POSITION(position, maxSpeed) panaSetZoomPosition(position, maxSpeed)

#define PAN_SPEED_SCALE(speedInt) (speedInt * 1.0)
#define TILT_SPEED_SCALE(speedInt) (speedInt * 1.0)
#define ZOOM_SPEED_SCALE(speedInt) (speedInt * 1.0)

#define PAN_AND_TILT_POSITION_SUPPORTED panaPanTiltPositionEnabled
#define ZOOM_POSITION_SUPPORTED panaZoomPositionEnabled

extern bool panaPanTiltPositionEnabled;
extern bool panaZoomPositionEnabled;

bool panaModuleInit(void);
bool panaSetIPAddress(char *address);
bool panaSetPanSpeed(double speed);
bool panaSetTiltSpeed(double speed);
bool panaSetZoomSpeed(double speed);
double panaGetPanPosition(void);
double panaGetTiltPosition(void);
double panaGetZoomPosition(void);
bool panaSetZoomPosition(double position, double maxSpeed);
int panaGetTallyState(void);

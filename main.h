#include <sys/types.h>

#pragma mark - Base implementation functions, callable from modules.

int debugGetTallyState(void);
bool debugSetPanSpeed(double speed);
bool debugSetTiltSpeed(double speed);
bool debugSetZoomSpeed(double speed);
double debugGetPanPosition(void);
double debugGetTiltPosition(void);
double debugGetZoomPosition(void);

bool setZoomPositionIncrementally(double position, double maxSpeed);


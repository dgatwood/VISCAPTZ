#include <sys/types.h>

#pragma mark - Base implementation functions, callable from modules.

int scaleSpeed(int speed, int fromScale, int toScale);

int debugGetTallyState(void);
bool debugSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed);
bool debugSetZoomSpeed(int64_t speed);
bool debugGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition);
int64_t debugGetZoomPosition(void);


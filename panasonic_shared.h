#include <stdbool.h>
#include <stdint.h>

extern bool pana_enable_debugging;

void populateZoomNonlinearityTable(void);
int64_t panaMakeZoomLinear(int64_t zoomPosition);
uint64_t scaleLinearZoomToRawZoom(uint64_t linearZoom);

/** Initializes the shared code. */
void panasonicSharedInit(int64_t **calibrationDataBuffer, int *maxSpeed);

/** Calibrates the Panasonic pan/tilt/zoom module. */
void panaModuleCalibrate(void);

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

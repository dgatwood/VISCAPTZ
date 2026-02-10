#include "panasonic_shared.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>

#include "configurator.h"
#include "main.h"
#include "panasonicptz.h"
#include "p2protocol.h"


/* This file contains code shared between Panasonic PTZ protocol and Panasonic P2 protocol. */

#if USE_PANASONIC_PTZ

// P2 protocol provides a location exactly twice per second.  We can fake more data than that
// when not calibrating, but in calibration mode, we have to wait for an actual UDP broadcast,
// so it doesn't matter how quickly we wake up; we will be blocked until half a second has passed.
#if ENABLE_P2_MODE
  #define RESPONSES_PER_SECOND 2
#else
  #define RESPONSES_PER_SECOND 5
#endif

#pragma mark - Constants

/** If true, enables extra debugging. */
bool pana_enable_debugging = false;

/** Maps hardware zoom values to linearized values. n=hw_zoom_positions+1 */
static int64_t *zoom_position_map;

static int zoom_position_map_count;

/** Integer key providing the zoomed in limit for calibration and recalibration. */
static const char *kZoomInInternalLimitKey = "zoom_in_internal_limit";

/** Integer key providing the zoomed out limit for calibration and recalibration. */
static const char *kZoomOutInternalLimitKey = "zoom_out_internal_limit";


#pragma mark - Data types

/** A motor/zoom position at a given time. */
typedef struct {
  int64_t position;
  double timeStamp;
} timed_position_t;


#pragma mark - Prototypes

void waitForZoomStop(timed_position_t **dataPointsOut, int *dataPointCountOut);


#pragma mark - Globals

/** The zoom calibration data in raw form (positions per second for each speed). */
int64_t *sharedCalibrationData = NULL;

#pragma mark - Functions

void panasonicSharedInit(int64_t **calibrationDataBuffer, int *maxSpeed) {
  *calibrationDataBuffer =
      readCalibrationDataForAxis(axis_identifier_zoom, maxSpeed);
  sharedCalibrationData = *calibrationDataBuffer;
}

// Holy &)*@^(& @^$&!  Panasonic's zoom positions aren't even linear.  Zooming out
// from the longest zoom position at the slowest speed moves at 48 positions per
// second, but zooming in at the slowest speed from the widest zoom position moves
// at only *4* positions per second!  I think it might actually be exponential or
// something insane.  This means we have to find some way to convert that into a
// linear range before we can do *anything* with it.  AAAAAAAARGH!
//
// Anyway, this converts a hardware zoom value into something roughly linear.
int64_t panaMakeZoomLinear(int64_t zoomPosition) {
  if (zoom_position_map_count == 0) {
    fprintf(stderr, "WARNING: Zoom position map empty.\n");
    return zoomPosition;
  }

  // Use the hardware zoom value as the baseline.
  int64_t minZoom = getConfigKeyInteger(kZoomOutInternalLimitKey);

  // The value of zoomPosition may be zero initially.  Clamp to minZoom so this
  // doesn't crash.
  int64_t entry = MAX(zoomPosition - minZoom, 0);  // Subtract the base.

  int64_t retval = zoom_position_map[entry];
  fprintf(stderr, "panaMakeZoomLinear: minZoom: %" PRId64 "\nEntry:%lld\nCount: %d\n", minZoom, entry, zoom_position_map_count);
  fprintf(stderr, "panaMakeZoomLinear: %lld -> %lld\n", zoomPosition, retval);

  return retval;
}

void populateZoomNonlinearityTable(void) {
  bool localDebug = true || pana_enable_debugging;
  int64_t minZoom = getConfigKeyInteger(kZoomOutInternalLimitKey);
  int64_t maxZoom = getConfigKeyInteger(kZoomInInternalLimitKey);

  if (localDebug) fprintf(stderr, "In populate: %lld %lld\n", minZoom, maxZoom);
  if (minZoom >= maxZoom || maxZoom == 0) {
    // Implausible data.
    fprintf(stderr, "Zoom range is broken (%lld to %lld).  Bailing.\n", minZoom, maxZoom);
    return;
  }

  zoom_position_map_count = maxZoom - minZoom + 1;
  zoom_position_map = malloc(zoom_position_map_count * sizeof(int64_t));

  int64_t position = 0;  // minZoom - minZoom
  int64_t scaledPosition = scaleLinearZoomToRawZoom(position);

  // If this assertion fails, your camera will require a different nonlinearity map.
  // Turn on debugging in printZoomNonlinearityComputation(), convert the table into
  // a third-degree equation with a polynomial solver (e.g. https://arachnoid.com/polysolve/)
  // and update the values in scaleLinearZoomToRawZoom(), guarding your changes with a
  // compile-time flag.  You can get the raw points by defining the
  // SHOW_NONLINEARITY_DATA_POINTS macro.

  if (localDebug) fprintf(stderr, "%d <= %lld\n", (int)floor(scaledPosition),  minZoom);
  assert(floor(scaledPosition) <= minZoom);

  int lastPosition = minZoom - 1;

  while (lastPosition < maxZoom) {
    // fprintf(stderr, "min/max: %lld %lld\n", minZoom, maxZoom);
    // fprintf(stderr, "pos %lld scaled %lld (between %lld and %lld, inclusive)\n",
    // position, scaledPosition, minZoom, maxZoom);
    if (scaledPosition > lastPosition) {
      lastPosition++;

      // Initially minZoom - 1 + 1 - minZoom = 0.
      // Terminally maxZoom - 1 + 1 - minZoom = zoom_position_map_count - 1 = max-min+1 -1 = max-min
      // So last time is when lastPosition = maxZoom - 1 + 1
      // So when lastPosition == maxZoom, drop out of the loop.
      uint64_t index = lastPosition - minZoom;
      zoom_position_map[index] = position;

      if (localDebug) {
        fprintf(stderr, "zoom_position_map[%lld] = %lld\n", index,
                zoom_position_map[index]);
      }
    }

    position++;
    scaledPosition = scaleLinearZoomToRawZoom(position);
    fprintf(stderr, "Scaled position: %lld\n", scaledPosition);
  }
  fprintf(stderr, "Nonlinearity table populated.\n");
}

// This is essentially the reverse of panaMakeZoomLinear(), but is exact rather than
// table-based, and is used in creating the table used by panaMakeZoomLinear().
uint64_t scaleLinearZoomToRawZoom(uint64_t linearZoom) {
  // After capturing zoom data from my camera in both directions, I computed
  // for a series of samples the sum of the opposite endpoint values and
  // the difference in their timestamps from the starting time, then moving
  // in opposite directions in the array.  Luckily, they had the same number
  // of items.  This gave an approximate average of the speed between the
  // two directions.
  //
  // From there, I determined that the fourth-degree polynomial of best fit was:
  //
  // f(x) =  1.3650286222285520e+003 * x^0
  //          +  3.8889564218704376e+000 * x^1
  //          +  1.2969582507813382e-002 * x^2
  //          + -1.8952897135302623e-004 * x^3
  //          +  2.7173948297895539e-006 * x^4
  //
  // This was with a range from 0 to 176 seconds.

  // Scale that range up by 100 so we go from 1 to 18k or so, thus stretching the
  // scale rather than compressing it.
  double scale = 0.01;

  double scaledZoom = linearZoom * scale;

  return 1.3650286222285520e+003
     +  3.8889564218704376 * scaledZoom
     +  0.012969582507813382 * pow(scaledZoom, 2)
     + -0.00018952897135302623 * pow(scaledZoom, 3)
     +  0.0000027173948297895539 * pow(scaledZoom, 4);
}

// Public function.  Docs in header.
//
// Creates speed tables indicating how fast the zoom axis moves at various
// speeds (in positions per second).
void panaModuleCalibrate(void) {
  bool localDebug = false;

  fprintf(stderr, "Calibrating zoom motors.  This takes about 20 minutes.\n");

  // Zoom all the way out, in preparation for capturing calibration data on the
  // way back out.
  SET_ZOOM_SPEED(-ZOOM_SCALE_HARDWARE, true);
  waitForZoomStop(NULL, NULL);

  setConfigKeyInteger(kZoomOutInternalLimitKey, GET_RAW_ZOOM_POSITION());

  #ifdef SHOW_NONLINEARITY_DATA_POINTS
    int count = 0;
    timed_position_t *positionArray = NULL;

    // By default, generate a smallish number of points.  You can use a
    // smaller number like 2 for more data.  The original equation was
    // computed at a speed of 2.
    const nonlinearityComputationSpeed = ZOOM_SCALE_HARDWARE / 2;

    // Zoom all the way in to determine the upper zoom limit and capture
    // calibration data to determine the nonlinearity of the zoom position data.
    SET_ZOOM_SPEED(/*ZOOM_SCALE_HARDWARE / 8 */ 2, true);
    fprintf(stderr, "Capturing positions per second for nonlinearity.");
    waitForZoomStop(&positionArray, &count);
    fprintf(stderr, "Done capturing positions per second for nonlinearity.");

    setConfigKeyInteger(kZoomInInternalLimitKey, GET_RAW_ZOOM_POSITION());

    // Currently, I have a hard-coded computation based on the data gathered here.
    // At some point, we could maybe automate this, but for now, just print the data.
    printZoomNonlinearityComputation(positionArray, count);
    free(positionArray);

    // Zoom all the way back out to determine the minimum zoom after applying the
    // nonlinearity compensation.
    SET_ZOOM_SPEED(-nonlinearityComputationSpeed, true);
    waitForZoomStop(&positionArray, &count);

    // Print in the reverse direction for comparison.
    printZoomNonlinearityComputation(positionArray, count);
    free(positionArray);
  #else

    // We have to provide kZoomInInternalLimitKey whether we're printing the
    // data for computing a new table or not!
    SET_ZOOM_SPEED(ZOOM_SCALE_HARDWARE, true);
    waitForZoomStop(NULL, NULL);
    setConfigKeyInteger(kZoomInInternalLimitKey, GET_RAW_ZOOM_POSITION());

    SET_ZOOM_SPEED(-ZOOM_SCALE_HARDWARE, true);
    waitForZoomStop(NULL, NULL);

  #endif

  // Populate the nonlinearity table so that panaGetZoomPosition() will provide
  // linearized values below.
  fprintf(stderr, "Populating nonlinearity table.\n");
  populateZoomNonlinearityTable();

  int64_t minimumZoom = GET_ZOOM_POSITION();

  if (localDebug) {
    fprintf(stderr, "Minimum zoom (raw): %" PRId64 "\n", GET_RAW_ZOOM_POSITION());
    fprintf(stderr, "Minimum zoom: %" PRId64 "\n", minimumZoom);
  }

  // Zoom all the way back in to determine the maximum zoom after applying the
  // nonlinearity compensation.
  SET_ZOOM_SPEED(ZOOM_SCALE_HARDWARE, true);
  waitForZoomStop(NULL, NULL);

  int64_t maximumZoom = GET_ZOOM_POSITION();

  if (localDebug) {
    fprintf(stderr, "Maximum zoom (raw): %" PRId64 "\n", GET_RAW_ZOOM_POSITION());
    fprintf(stderr, "Maximum zoom: %" PRId64 "\n", maximumZoom);
  }

  setZoomOutLimit(minimumZoom);
  setZoomInLimit(maximumZoom);
  setZoomEncoderReversed(false);
  setZoomMotorReversed(false);

  fprintf(stderr, "Done determining endpoints.\n");

  int64_t *zoomCalibrationData = calibrationDataForMoveAlongAxis(
      axis_identifier_zoom, maximumZoom, minimumZoom, ZOOM_MIN_HARDWARE, ZOOM_SCALE_HARDWARE, true);

  writeCalibrationDataForAxis(axis_identifier_zoom, zoomCalibrationData, ZOOM_SCALE_HARDWARE);
}


#pragma mark - Calibration

/** Polls the zoom position 5x per second until it stops for at least a second. */
void waitForZoomStop(timed_position_t **dataPointsOut, int *dataPointCountOut) {
  bool localDebug = false;
  int64_t lastPosition = GET_RAW_ZOOM_POSITION();
  int stoppedCount = 0;

  int dataPointCount = 1;
  timed_position_t *dataPoints = (dataPointsOut == NULL) ? NULL : malloc(sizeof(timed_position_t));

  if (dataPointsOut != NULL) {
    dataPoints[0].position = lastPosition;
    dataPoints[0].timeStamp = timeStamp();
  }

  if (localDebug) {
    fprintf(stderr, "Initial position: %" PRId64 "\n", lastPosition);
  }

  // If no motion for a second, we're done, unless we never started, in which
  // case wait a little longer.
  while (stoppedCount < ((dataPointCount <= 1) ? RESPONSES_PER_SECOND * 10 : RESPONSES_PER_SECOND)) {
    int64_t currentPosition = GET_RAW_ZOOM_POSITION();

    if (localDebug) {
      fprintf(stderr, "Current position: %" PRId64 "\n", currentPosition);
    }

    if (currentPosition == lastPosition) {
      stoppedCount++;
    } else {
      stoppedCount = 0;
      dataPointCount++;
      if (dataPointsOut != NULL) {
        dataPoints = realloc(dataPoints, dataPointCount * sizeof(timed_position_t));
        dataPoints[dataPointCount - 1].position = currentPosition;
        dataPoints[dataPointCount - 1].timeStamp = timeStamp();
      }
    }
    lastPosition = currentPosition;
    usleep(200000);
  }
  if (dataPointsOut != NULL) {
    *dataPointsOut = dataPoints;
  }
  if (dataPointCountOut != NULL) {
    *dataPointCountOut = dataPointCount;
  }
}

void printZoomNonlinearityComputation(timed_position_t *positionArray, int count) {
  fprintf(stderr, "NLC\n");
  for (int i = 0 ; i < count; i++) {
    fprintf(stderr, "%d\t%lf\t%lld\n", i,
            positionArray[i].timeStamp - positionArray[0].timeStamp,
            positionArray[i].position);
  }
}

// Public function.  Docs in header.
//
// Returns the number of pan positions per second at the camera's slowest speed.
int64_t panaMinimumPanPositionsPerSecond(void) {
  #if !PANASONIC_PTZ_ZOOM_ONLY
    return minimumPositionsPerSecondForData(pana_pan_data, PAN_TILT_SCALE_HARDWARE);
  #else
    return 0;
  #endif
}

// Public function.  Docs in header.
//
// Returns the number of tilt positions per second at the camera's slowest speed.
int64_t panaMinimumTiltPositionsPerSecond(void) {
  #if !PANASONIC_PTZ_ZOOM_ONLY
    return minimumPositionsPerSecondForData(pana_tilt_data, PAN_TILT_SCALE_HARDWARE);
  #else
    return 0;
  #endif
}

// Public function.  Docs in header.
//
// Returns the number of zoom positions per second at the camera's slowest speed.
int64_t panaMinimumZoomPositionsPerSecond(void) {
  return minimumPositionsPerSecondForData(sharedCalibrationData, ZOOM_SCALE_HARDWARE);
}

// Public function.  Docs in header.
//
// Returns the number of pan positions per second at the camera's fastest speed.
int64_t panaMaximumPanPositionsPerSecond(void) {
  #if !PANASONIC_PTZ_ZOOM_ONLY
    return pana_pan_data[PAN_TILT_SCALE_HARDWARE];
  #else
    return 0;
  #endif
}

// Public function.  Docs in header.
//
// Returns the number of tilt positions per second at the camera's fastest speed.
int64_t panaMaximumTiltPositionsPerSecond(void) {
  #if !PANASONIC_PTZ_ZOOM_ONLY
    return pana_tilt_data[PAN_TILT_SCALE_HARDWARE];
  #else
    return 0;
  #endif
}

// Public function.  Docs in header.
//
// Returns the number of zoom positions per second at the camera's fastest speed.
int64_t panaMaximumZoomPositionsPerSecond(void) {
  return sharedCalibrationData[ZOOM_SCALE_HARDWARE];
}

#endif  // USE_PANASONIC_PTZ

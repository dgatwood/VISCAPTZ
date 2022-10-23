#include "configurator.h"
#include "constants.h"
#include "main.h"
#include "motorptz.h"
#include "obs_tally.h"
#include "panasonicptz.h"
#include "tricaster_tally.h"

#include <arpa/inet.h>
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>


#pragma mark - Configuration keys

/** Boolean key indicating that the pan motor direction is reversed. */
const char *kPanMotorReversedKey = "pan_axis_motor_reversed";

/** Boolean key indicating that the tilt motor direction is reversed. */
const char *kTiltMotorReversedKey = "tilt_axis_motor_reversed";

/** Boolean key indicating that the pan encoder direction is reversed. */
const char *kPanEncoderReversedKey = "pan_axis_encoder_reversed";

/** Boolean key indicating that the tilt encoder direction is reversed. */
const char *kTiltEncoderReversedKey = "tilt_axis_encoder_reversed";

/** Integer key providing the left pan limit for calibration and recalibration. */
const char *kPanLimitLeftKey = "pan_limit_left";

/** Integer key providing the right pan limit for calibration and recalibration. */
const char *kPanLimitRightKey = "pan_limit_right";

/** Integer key providing the upper tilt limit for calibration and recalibration. */
const char *kTiltLimitTopKey = "tilt_limit_up";

/** Integer key providing the lower tilt limit for calibration and recalibration. */
const char *kTiltLimitBottomKey = "tilt_limit_down";

/** Integer key providing the zoomed in limit for calibration and recalibration. */
const char *kZoomInLimitKey = "zoom_in_limit";

/** Integer key providing the zoomed out limit for calibration and recalibration. */
const char *kZoomOutLimitKey = "zoom_out_limit";

/** Boolean key indicating that the zoom encoder sends larger values when zoomed out. */
const char *kZoomEncoderReversedKey = "zoom_encoder_reversed";

/** String key containing the camera IP address for the Panasonic module. */
const char *kCameraIPKey = "camera_ip_address";

/** Boolean key indicating that the zoom motor uses larger values to zoom out. */
const char *kZoomMotorReversedKey = "zoom_motor_reversed";

#if USE_TRICASTER_TALLY_SOURCE
  /** String key containing the switcher IP address for the Tricaster module. */
  const char *kTricasterIPKey = "tricaster_ip_address";
#endif

/** String key containing the tally source name for the Tricaster or OBS module. */
const char *kTallySourceName = "tally_source_name";

#if USE_OBS_TALLY_SOURCE
  /** String key containing the websocket URL for the OBS module. */
  const char *kOBSWebSocketURLKey = "obs_websocket_url";

  /** String key containing the websocket password for the OBS module. */
  const char *kOBSPasswordKey = "obs_websocket_password";
#endif


#pragma mark - Other constants

/**
 * The maximum number of times we will try to move the motor if the
 * encoder value does not change.
 */
static const int kAxisStallThreshold = 100;

/**
 * The decipercentage of the motion that should be used for ramping up or down.
 */
static const int kRampUpPeriodEnd = 200;

// Do not change.  The ramp up and ramp down periods must be equal.
static const int kRampDownPeriodStart = 1000 - kRampUpPeriodEnd;

// During the ramp up/down period, the axis moves at half speed, so each of these periods
// moves half the distance. Therefore, the percentage of the total distance moved at maximum
// speed is halfway between 100% and the percentage of time spent at maximum speed.  So for
// a ramp-up/ramp-down period of 10%, it moves at maximum speed for 90% of the distance.
//
// Thus, the slowdown begins after the initial ramp-up period's 5% plus that 90%, or 95%.
//
// Assuming the ramp-up period and ramp-down period are equal, this can be simplified to
// one minus half of the ramp-up period.
static const double moveDistanceFractionBeforeSlowdown = 1 - ((kRampUpPeriodEnd / 1000) / 2);

// The time fraction mefore slowdown is just kRampDownPeriodStart / 1000.
static const double moveTimeFractionBeforeSlowdown = (kRampDownPeriodStart / 1000.0);

/**
 * A constant that indicates that a value (pan, tilt, or zoom) in a move
 * call is unused because of the associated moveModeFlags value.
 */
static int kUnusedPosition = 0;  // Tells programmers that a value in a move call is unused.

/** Flags used to control which parameters of a move call are used.  */
typedef enum {
  kFlagMovePan = 0x1,             //! Use the pan axis position.
  kFlagMoveTilt = 0x2,            //! Use the tilt axis position.
  kFlagMoveZoom = 0x4,            //! Use the zooom axis position.
} moveModeFlags;


#pragma mark - Data structures

/** A data structure representing a VISCA request (command/inquiry) packet over the wire. */
typedef struct {
  uint8_t cmd[2];
  uint16_t len;  // Big endian!  (Network byte order.)
  uint32_t sequence_number;
  uint8_t data[65519];  // Maximum theoretical IPv6 UDP packet size minus above.
} visca_cmd_t;

/** A data structure representing a VISCA response packet over the wire. */
typedef struct {
  uint8_t cmd[2];
  uint16_t len;  // Big endian!  (Network byte order.)
  uint32_t sequence_number;
  uint8_t data[65527];  // Maximum theoretical IPv6 UDP packet size.
} visca_response_t;

/** A data structure representing a preset on disk. */
typedef struct {
    int64_t panPosition, tiltPosition, zoomPosition;
} preset_t;


#pragma mark - Global variables

// General state

/** True if VISCA commands use core scale, else false. */
bool gVISCAUsesCoreScale = false;

/** True if in calibration mode, else false. */
bool gCalibrationMode = false;

/**
 * True if recalibrating pan/tilt/zoom speeds without reconfiguring the
 * bounds and axis directions.
 */
bool gCalibrationModeQuick = false;

/** True if recalibrating only the zoom speed (e.g. when switching cameras). */
bool gCalibrationModeZoomOnly = false;

/**
 * True during phases of calibration in which VISCA controls
 * should be disabled.
 */
bool gCalibrationModeVISCADisabled = false;

/**
 * True if the encoders should update their center position based on their
 * current position.
 */
bool gRecenter = false;

/**
 * The thread used for the VISCA listener.  This is on a separate
 * thread because when in calibration modes, the main thread must
 * do additional work.
 */
pthread_t gVISCANetworkThread;


// Data about the current automated move (recalls, absolute positioning calls, etc.)

/** True if the specified axis is (still) involved in the currently active move. */
static bool gAxisMoveInProgress[NUM_AXES];

/** The start position of a given axis for the currently active move. */
static int64_t gAxisMoveStartPosition[NUM_AXES];

/** The ending position of a given axis for the currently active move. */
static int64_t gAxisMoveTargetPosition[NUM_AXES];

/**
 * The maximum speed of a given axis for the currently active move.  This appears
 * to be completely unused.
 */
static int64_t gAxisMoveMaxSpeed[NUM_AXES];

/**
 * The most recent speed at which the specified axis was moving in the currently
 * active move.
 */
static int64_t gAxisLastMoveSpeed[NUM_AXES];

/**
 * The position of the specified axis at the previous move interval (1/100th of
 * a second).  Used to determine if the motor has stalled at the end of an
 * uncalibrated move to avoid wasting power (forever) unnecessarily.
 */
static int64_t gAxisPreviousPosition[NUM_AXES];

/**
 * The timestamp when gAxisPreviousPosition was last updated.  Used for debugging
 * speed calculations.
 */
static double gAxisPreviousPositionTimestamp[NUM_AXES];

/**
 * The desired duration of the move on the specified axis in seconds (relative
 * to gAxisStartTime).
 */
static double gAxisDuration[NUM_AXES];

/**
 * The timestamp (in floating-point seconds since the ephoc) at which the
 * current move began.
 */
static double gAxisStartTime[NUM_AXES];

/**
 * The number of consecutive checks of the position of the axis in which
 * the position value did not change.  Used to stop moving early if the
 * motor has stalled during an uncalibrated move, rather than wasting
 * power forever.
 */
static int gAxisStalls[NUM_AXES];

/**
 * The current tally state as set by VISCA commands.  Used only if
 * the VISCA tally source is active.
 */
volatile tallyState gTallyState = kTallyStateOff;

/**
 * Set to true if a VISCA command has specified a particular recall speed.
 * If this has not yet happened, the recall speed is determined based on
 * whether the camera is live or not.
 */
bool gRecallSpeedSet = false;

/** The recall speed as set via VISCA. */
int gVISCARecallSpeed = 0;


#pragma mark - Prototypes

/** Returns a printable string containing the name of an axis. */
const char *nameForAxis(axis_identifier_t axis);

// Absolute positioning/recall prototypes

/** Returns true if absolute positioning is supported for the specified axis. */
bool absolutePositioningSupportedForAxis(axis_identifier_t axis);

/** Cancels any active recalls. */
void cancelRecallIfNeeded(char *context);

/**
 * Returns the duration that should be used for a recall if performed right now,
 * By default, this returns a duration based on whether the camera is in preview
 * mode or program mode.  If a maximum speed is set via VISCA, that speed takes
 * precedence.
 *
 * This function ignores whether the duraton is actually possible.  For that,
 * you should call durationForMove.
 */
double currentRecallTime(void);

/**
 * Computes the duration for a move to the specified positions on the specified axes.
 *
 * @param flags A set of flags that defines which axes are used.
 *
 */
double durationForMove(moveModeFlags flags, int64_t panPosition, int64_t tiltPosition, int64_t zoomPosition);

/**
 * Computes the maximum speed (in core speed) that the motor should reach
 * when moving the specified distance over the specified time.
 */
int peakSpeedForMove(axis_identifier_t axis, int64_t fromPosition, int64_t toPosition, double time);

/**
 * Returns the fastest possible move that the camera can make to the specified
 * position on the specified axis (measured in seconds).
 */
double fastestMoveForAxisToPosition(axis_identifier_t axis, int64_t position);

/**
 * Returns the current position of the specified axis.
 *
 * If the specified axis does not support absolute positioning, this function returns zero (0).
 */
int64_t getAxisPosition(axis_identifier_t axis);

/** Updates the speed of axes that are being moved incrementally under programmatic control. */
void handleRecallUpdates(void);

/** Returns the number of positions that a given axis moves in a second at its maximum speed. */
int64_t maximumPositionsPerSecondForAxis(axis_identifier_t axis);

/**
 * Sets the position of an axis to the specified position.
 *
 * @param maxSpeed The maximum speed for the axis.  This value appears to be unused.
 * @param duration The target duration.
 */
bool setAxisPositionIncrementally(axis_identifier_t axis, int64_t position, int64_t maxSpeed, double duration);

/** Sets the axis speed, using core scale speed values. */
bool setAxisSpeed(axis_identifier_t axis, int64_t coreSpeed, bool debug);

/** Sets the axis speed, using values based on the hardware scale for that axis. */
bool setAxisSpeedRaw(axis_identifier_t axis, int64_t speed, bool debug);

/** Do not use directly.  Call setAxisSpeed or setAxisSpeedRaw instead. */
bool setAxisSpeedInternal(axis_identifier_t axis, int64_t speed, bool debug, bool isRaw);

/**
 * Returns the slowest possible move that the camera can make to the specified
 * position on the specified axis (measured in seconds).
 */
double slowestMoveForAxisToPosition(axis_identifier_t axis, int64_t position);

/** Clamps a move direction based on the maximum and minimum speeds for that axis.  */
double makeDurationValid(axis_identifier_t axis, double duration, int64_t position);

// Preset management

/**
 * Recalls the specified preset and moves the camera to that position.
 *
 * Legal preset values are in the range 0..255, but the code doesn't check, so use
 * higher numbers for internal tests, etc.
 */
bool recallPreset(int presetNumber);

/**
 * Stores the current position into the specified preset.
 *
 * Legal preset values are in the range 0..255, but the code doesn't check, so use
 * higher numbers for internal tests, etc.
 */
bool savePreset(int presetNumber);


// VISCA-related functions

/** Stringifies a VISCA command or inquiry for debugging. */
char *VISCAMessageDebugString(uint8_t *command, uint8_t len);

/**
 * Gets a default zoom speed (in VISCA range) based on the current tally state,
 * e.g. the speed is slower when the camera is live.
 */
int getVISCAZoomSpeedFromTallyState(void);

/** Processes a single VISCA command or inquiry packet. */
bool handleVISCAPacket(visca_cmd_t command, int sock, struct sockaddr *client, socklen_t structLength);

/* Sets the speed for future recall operations (from a VISCA source, using VISCA scale). */
void setRecallSpeedVISCA(int value);

/** Returns a canned VISCA response suitable for sending in response to an error. */
visca_response_t *failedVISCAResponse(void);

/** Returns a canned VISCA response suitable for sending in response to an enqueued command. */
visca_response_t *enqueuedVISCAResponse(void);

/** Returns a canned VISCA response suitable for sending in response to a completed command. */
visca_response_t *completedVISCAResponse(void);


// Miscellaneous prototypes.

/** The main function of the VISCA network thread. */
void *runNetworkThread(void *argIgnored);

/** Runs a series of tests for built-in conversion functions. */
void runStartupTests(void);

/** Returns the current time in seconds and fractions of a second. */
double timeStamp(void);


/**
 * Sends the provided VISCA response, modified to use the specified sequence number,
 * over the specified socket, using the specified source address and source address
 * length.
 */
bool sendVISCAResponse(visca_response_t *response, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength);

/** Handles a VISCA inquiry packet. */
bool handleVISCAInquiry(uint8_t *command, uint8_t len, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength);

/** Handles a VISCA command packet. */
bool handleVISCACommand(uint8_t *command, uint8_t len, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength);

/** Populates the specified VISCA response with the specified data bytes. */
#define SET_RESPONSE(response, array) setResponseArray(response, array, (uint8_t)(sizeof(array) / sizeof(array[0])))

/** Helper function for SET_RESPONSE. */
void setResponseArray(visca_response_t *response, uint8_t *array, uint8_t count);

// ABOUT SPEEDS
//
// This code base has three speed scales:
//
//   * VISCA speeds:    -24 to 24
//   * Core speeds:     -1000 to 1000
//   * Hardware speeds: Depends on the hardware
//
// The reason for this is so that we can do very smooth adjustments of
// speed, where possible, when performing s-curve computations at the
// start and end of automatic motion to a specific location.
//
// Hardware speed info:
//   * Motor control: ???
//   * Panasonic zoom (CGI): -49 to 49

bool resetCalibration(void);
void do_calibration(void);

#if USE_TRICASTER_TALLY_SOURCE
  /** Stores the Tricaster IP address into settings (used for Tricaster tally sources). */
  void setTricasterIP(char *TricasterIP);
#endif

#if TALLY_SOURCE_NAME_REQUIRED
  /** Stores the tally source/scene name (used by OBS and Tricaster tally sources) into settings. */
  void setTallySourceName(char *tallySourceName);
#endif

#ifdef SET_IP_ADDR
  /** Stores the camera's IP address into settings (used for Panasonic cameras). */
  void setCameraIP(char *TricasterIP);
#endif

#if USE_OBS_TALLY_SOURCE
  /** Stores the WebSocket URL (used by OBS tally sources) into settings. */
  void setOBSWebSocketURL(char *OBSWebSocketURL);

  /** Stores the WebSocket password (used by OBS tally sources) into settings. */
  void setOBSPassword(char *password);
#endif


#pragma mark - Main

bool debug_verbose = false;

int main(int argc, char *argv[]) {

  runStartupTests();

  if (argc >= 2) {
    if (!strcmp(argv[1], "-v")) {
      debug_verbose = true;
    } else if (!strcmp(argv[1], "--calibrate")) {
      gCalibrationMode = true;

      // Immediately wipe out calibration data before initializing modules,
      // to ensure that the motor driver does not load any of the old
      // calibration data when we initialize it below.
      resetCalibration();
    } else if (!strcmp(argv[1], "--recalibrate")) {
      // Reruns the calibration using the same stop positions.  Used for
      // debugging the calibration process without having to manually do the
      // panning and tilting at the beginning.
      gCalibrationMode = true;
      gCalibrationModeQuick = true;
    } else if (!strcmp(argv[1], "--zoomcalibrate")) {
      // Reruns the zoom calibration while leaving the pan and tilt alone.
      // Use when switching cameras.
      gCalibrationMode = true;
      gCalibrationModeQuick = true;
      gCalibrationModeZoomOnly = true;
    } else if (!strcmp(argv[1], "--recenter")) {
      gRecenter = true;
#if USE_MOTOR_PAN_AND_TILT
    } else if (!strcmp(argv[1], "--setswappedmotors")) {
      if (argc < 3) {
        fprintf(stderr, "Usage: viscaptz --setswappedmotors [0|1]\n");
        exit(1);
      }
      setConfigKeyBool(kMotorsAreSwappedKey, atoi(argv[2]));

      exit(0);
#endif
#if TALLY_SOURCE_NAME_REQUIRED
    } else if (!strcmp(argv[1], "--settallysourcename")) {
      if (argc < 3) {
        fprintf(stderr, "Usage: viscaptz --settallysourcename <Source/Scene Name>\n");
        exit(1);
      }
      setTallySourceName(argv[2]);
      exit(0);
#endif
#ifdef SET_IP_ADDR
    } else if (!strcmp(argv[1], "--setcameraip")) {
      if (argc < 3) {
        fprintf(stderr, "Usage: viscaptz --setcameraip <Camera IP>\n");
        exit(1);
      }
      setCameraIP(argv[2]);
      exit(0);
#endif
#if USE_TRICASTER_TALLY_SOURCE
    } else if (!strcmp(argv[1], "--settricasterip")) {
      if (argc < 3) {
        fprintf(stderr, "Usage: viscaptz --settricasterip <Tricaster IP>\n");
        exit(1);
      }
      setTricasterIP(argv[2]);
      exit(0);
#endif
#if USE_OBS_TALLY_SOURCE
    } else if (!strcmp(argv[1], "--setobsurl")) {
      if (argc < 3) {
        fprintf(stderr, "Usage: viscaptz --setobsurl <WebSockets URL>\n");
        exit(1);
      }
      setOBSWebSocketURL(argv[2]);
      exit(0);
    } else if (!strcmp(argv[1], "--setobspass")) {
      if (argc < 3) {
        fprintf(stderr, "Usage: viscaptz --setobspass <password>\n");
        exit(1);
      }
      setOBSPassword(argv[2]);
      exit(0);
#endif
    }
  }
#if USE_CANBUS && ENABLE_ENCODER_HARDWARE && ENABLE_HARDWARE
  if (argc >= 4) {
    if (!strcmp(argv[1], "--reassign")) {
      int oldCANBusID = atoi(argv[2]);
      int newCANBusID = atoi(argv[3]);
      reassign_encoder_device_id(oldCANBusID, newCANBusID);
    }
  }
#endif

  pthread_create(&gVISCANetworkThread, NULL, runNetworkThread, NULL);

#ifdef SET_IP_ADDR
  char *cameraIP = getConfigKey(kCameraIPKey);
  if (cameraIP == NULL) {
    fprintf(stderr, "This software is configured for an IP-based camera, but you have not set\n");
    fprintf(stderr, "an IP-based address.  Run viscaptz --setcameraip to fix this.\n");
    exit(1);
  }
  SET_IP_ADDR(cameraIP);
#endif

  if (!obsModuleInit()) {
    fprintf(stderr, "OBS module init failed.  Bailing.\n");
    exit(1);
  }
  if (!tricasterModuleInit()) {
    fprintf(stderr, "Tricaster module init failed.  Bailing.\n");
    exit(1);
  }
  if (!motorModuleInit()) {
    fprintf(stderr, "Motor module init failed.  Bailing.\n");
    exit(1);
  }
  if (!panaModuleInit()) {
    fprintf(stderr, "Panasonic module init failed.  Bailing.\n");
    exit(1);
  }

  fprintf(stderr, "Created threads.\n");

  if (gCalibrationMode) {
    fprintf(stderr, "Starting calibration.\n");
    fprintf(stderr, "Starting calibration.  Move the camera as far to the left as possible, then as far to the\n");
    fprintf(stderr, "right as possible.  Then move the camera as far up as possible, then as far down as possible.\n");
    fprintf(stderr, "The order is important.  Make sure that your last horizontal move is to the right, and your\n");
    fprintf(stderr, "last vertical move is downwards.\n\n");
    fprintf(stderr, "When you are done, wait for a few seconds, and calibration will begin automatically.\n");
    do_calibration();
  }

  fprintf(stderr, "Ready for VISCA commands.\n");

  // Spin this thread forever for now.
  while (1) {
    usleep(1000000);
  }
}


#pragma mark - Generic move routines

int actionProgress(int axis, int64_t startPosition, int64_t curPosition, int64_t endPosition,
                   int64_t previousPosition, int *stalls, bool usingTimeComputation) {
  bool localDebug = false;
  int64_t progress = llabs(curPosition - startPosition);
  int64_t total = llabs(endPosition - startPosition);

  if (total == 0) {
    if (localDebug) {
      fprintf(stderr, "Axis %d End position (%" PRId64 ") = start position (%" PRId64 ").  Doing nothing.\n",
              axis, startPosition, endPosition);
    }
    return 1000;
  }

  if ((startPosition != curPosition) && (previousPosition == curPosition)) {
    // We may have slowed down to the point where the motors no longer move.
    // Don't keep wasting power and heating up the motors.
    *stalls = (*stalls) + 1;
    if (*stalls > kAxisStallThreshold) {
      return 1000;
    }
  } else {
    *stalls = 0;
  }

  int tenth_percent = (int)llabs((1000 * progress) / total);  // Value will never be much over 1000.

  // If we're moving too quickly, hit the brakes.  Don't do this if we have exact motor speed data
  // and are using time-based measurements to compute the speed, because it needs to know the exact
  // position to determine if it is deviating too much from the expected position, and this would
  // cause the motor to slow down way too early.
  if (!usingTimeComputation) {
    int64_t increment = llabs(curPosition - previousPosition);
    if ((increment + progress) > total) {
      tenth_percent = MAX(975, tenth_percent);
    } else if ((progress + (2 * increment)) > total) {
      tenth_percent = MAX(950, tenth_percent);
    } else if ((progress + (4 * increment)) > total) {
      tenth_percent = MAX(kRampDownPeriodStart, tenth_percent);
    }
    if (localDebug) {
      fprintf(stderr, "actionProgress: increment=% "PRId64 ", previous=%" PRId64 "\n",
              increment, previousPosition);
    }
  }

  if (localDebug) {
    fprintf(stderr, "actionProgress: start=%" PRId64 ", end=%" PRId64 ", cur=%" PRId64 "\n",
            startPosition, endPosition, curPosition);
    fprintf(stderr, "actionProgress: progress=%" PRId64 ", total=%" PRId64 ", tenth_percent=%d\n",
            progress, total, tenth_percent);
  }

  return (tenth_percent < 0) ? 0 : (tenth_percent > 1000) ? 1000 : tenth_percent;
}

/// Computes the speed for pan, tilt, and zoom motors on a scale of -1000 to 1000 (core speed).
///
///     @param progress         How much progress has been made (in tenths of a percent
///                             of the total move length).
///     @param peakSpeed        The computed maximum speed for the move, computed by
///                             a prior call to peakSpeedForMove().
int computeSpeed(int progress, int peakSpeed) {
    if (progress < kRampUpPeriodEnd || progress > kRampDownPeriodStart) {
        int decipercentProgressToStartOrEnd = (progress >= kRampDownPeriodStart) ? (1000 - progress) : progress;

        // Depending on whether you run this function before or after calibrating the
        // motor speed, the progress value is either based on the percentage of progress
        // position-wise or time-wise.  The former is extremely inaccurate, giving fairly
        // jerky motion, but that's the best you can do without knowing the maximum speed
        // of the motor.
        //
        // After you calibrate the motor speed, this uses a time-based curve.  The explanation
        // below tells how this works.
        //
        // From 20% to 80%, the motor moves at its maximum speed.  On either end, the motor's
        // speed is computed based on an s-curve, computed as follows:
        //
        // With vertical acccuracy of ± .1% at the two endpoints (i.e. 0.001 at the bottom,
        //     0.999 at the top):
        //
        // 1 / (1 + e^-x) would give us a curve of length 14 from -7 to 7.
        // 1 / (1 + e^-(x*7)) would give us a curve of length 14 from -1 to 1.
        // 1 / (1 + e^-((x*7 / 50)) would give us a curve of length 100 from -50 to 50.
        // 1 / (1 + e^-(((x - 50)*7 / 50)) gives us a curve from 0 to 100 (one tenth the
        //                                 progress period).
        // 1 / (1 + e^-(7x/50 - 7)) is simplified.
        // 1 / (1 + e^(7 - (7x/50))) is fully simplified.
        //
        // The integral of this is -(1/7)((-50 * ln (1+e^(7-(7x /50))))-7x+350).
        // Evaluated from 0 to 100, this gives us 50.0065104747 - 0.00651047466,
        // or exactly an area of 50 under the curve.  So the s-curves average 50%
        // across their duration.
        //
        // This means that for an axis that can move at maxPositionsPerSecond positions
        // per second at its fastest speed, the average speed across the entire time range
        // from 0 to 1000 is
        //
        //   ((maxPositionsPerSecond * 800) + (0.5 * maxPositionsPerSecond * 200)) / 1000
        //
        // which can be simplified to 0.9 * maxPositionsPerSecond.  The same thing applies
        // proportionally if the ramp period is longer, i.e. the average speed is equal to
        // (1 - rampFraction) * maxPositionsPerSecond.
        //
        // So if we know that we want a motion to take 10 seconds (for example), and if
        // that motion is 5000 units of distance, it needs to move 500 units per second,
        // on average.  Since we know that the average speed of the move, including the
        // two curves, will be only 0.9 times the speed that the motor moves during the
        // middle 80% of the move, that means that the peak speed during the middle 80%
        // should be 10/9ths of 500, or about 555.55.
        //
        // With that information, we can calculate the exact fraction of maximum speed
        // that the motors should run at their peak to achieve a move of a given
        // duration, and from there, we can compute what the speed should be at any
        // given point in time from the start of the movement to the end.
        //
        // However, motors are nonlinear.  They don't move at all at low power, and
        // their speed isn't exactly linear in the duty cycle.  (This actually requires
        // a workaround just to get motion at all without calibration data, because
        // we have to prevent it from moving at zero speed, or else the progress
        // will never increase, and it will never speed up.)
        //
        // So to compute the actual output levels correctly, we need to do a calibration
        // stage.  Here's how that works:
        //
        // 1.  The user runs ./viscaptz --calibrate
        // 2.  The user moves the camera to the leftmost position that is safe to use,
        //     followed by the rightmost position.
        // 3.  The user moves the camera to the maximum upwards position that is safe
        //     to use, followed by the maximum downwards position.
        // 4.  The user waits ten seconds without moving the camera.  The software
        //     interprets the last horizontal move direction to be right, and the last
        //     vertical move direction to be down, and will invert the motion on each
        //     axis as needed to ensure that VISCA left/right/up/down moves go in the
        //     right direction.  The software also recognizes whether the encoder
        //     positions increase or decrease as the camera moves in each direction,
        //     and uses that to determine which way to move based on whether the
        //     target position is a smaller or larger number.
        //
        // At this point, automated calibration continues as follows:
        //
        // 1.  The camera repeatedly pans back and forth, computing the number
        //     of positions per second at each speed.  It performs this computation
        //     several times, throws out any outliers (beyond one standard deviation),
        //     and then computes the average.
        //
        //     If the computed positions-per-second value for any speed is slower
        //     than the computed positions-per-second value at the previous speed
        //     value, the loop goes back and recomputes the previous speed value
        //     (in case that value was high) and then the current speed value.
        //     This is, unfortunately, always going to be an approximation, because
        //     drag on the motor won't be constant (wires drag on the tripod), the
        //     input voltage may not be constant, etc.
        //
        // 2.  The camera performs a similiar calibration for the tilt.
        // 3.  The camera performs a similiar calibration for the zoom.
        //
        // The result is a table of positions-per-second values for each hardware speed
        // value.  From there, the hardware module scales these values so that the
        // highest encoder-positions-per-second value is converted to 1000 and all
        // smaller values are scaled proportionally.
        //
        // When computing the hardware speed for a given core speed (scale -1000 to 1000),
        // the scaling function then can use the scaled values to determine how close
        // each hardware speed is to the expected values, and can adjust them accordingly.

        // The value of decipercentProgressToStartOrEnd is in the range 0..kRampUpPeriodEnd.
        // Convert that to the range 0..100.
        double percentOfRampDuration = (decipercentProgressToStartOrEnd * 100.0) / kRampUpPeriodEnd;
        double exponent = 7.0 - ((7.0 * percentOfRampDuration) / 50.0);
        double speedFromProgress = 1 / (1 + pow(M_E, exponent));
        return round(speedFromProgress * 1.0 * peakSpeed);
    } else {
        return peakSpeed;
    }
}

bool moveInProgress(void) {
  for (axis_identifier_t axis = axis_identifier_pan ; axis < NUM_AXES; axis++) {
    if (gAxisMoveInProgress[axis]) {
      return true;
    }
  }
  return false;
}

void handleRecallUpdates(void) {
  int localDebug = 0;

  for (axis_identifier_t axis = axis_identifier_pan ; axis < NUM_AXES; axis++) {
    if (gAxisMoveInProgress[axis]) {
      if (localDebug) {
        fprintf(stderr, "UPDATING AXIS %d\n", axis);
      }
      int64_t startPosition = gAxisMoveStartPosition[axis];
      int64_t targetPosition = gAxisMoveTargetPosition[axis];

      // Compute how far into the motion we are (with a range of 0 to 1,000).
      int direction = (targetPosition > startPosition) ? 1 : -1;
      if (localDebug) {
        fprintf(stderr, "Axis %d direction %d\n", axis, direction);
      }

      // Left/up values are treated as positive (ignoring any inversion required if the motor is
      // backwards).  Right/down are negative.
      //
      // Normal encoder:   Higher values are left.  So a higher value (left of current) means
      //                   positive motor speeds
      // Reversed encoder: Higher values are right.  So a higher value (right of current) means
      //                   negative motor speeds.
      if ((axis == axis_identifier_pan && panEncoderReversed()) ||
          (axis == axis_identifier_tilt && tiltEncoderReversed()) ||
          (axis == axis_identifier_zoom && zoomEncoderReversed())) {
        if (localDebug) {
          fprintf(stderr, "Axis %d reversed\n", axis);
        }
        direction = -direction;
        if (localDebug) {
          fprintf(stderr, "Axis %d direction now %d\n", axis, direction);
        }
      } else if (localDebug) {
        fprintf(stderr, "Axis %d not reversed.\n", axis);
      }

      if (localDebug) {
        fprintf(stderr, "Axis %d updated direction %d\n", axis, direction);
      }
      int64_t axisPosition = getAxisPosition(axis);
#if EXPERIMENTAL_TIME_PROGRESS
      double duration = gAxisDuration[axis];
#else
      double duration = 0;
#endif
      int moveProgressByPosition = actionProgress(axis, startPosition, axisPosition,
                                                  targetPosition, gAxisPreviousPosition[axis],
                                                  &gAxisStalls[axis], (duration != 0));
#if EXPERIMENTAL_TIME_PROGRESS
      double currentTime = timeStamp();
      double remainingTime = gAxisStartTime[axis] + duration - currentTime;

      // Time-based computation can be slightly imprecise.  If the progress based on position is
      // 1000, we're done with this axis no matter what the wall clock says.  And of course, if
      // there's no computed duration, we use the position-based approach, and if the duration
      // is actually zero (no motion needed), then we also use the position-based approach to
      // guarantee that we don't move.
      bool usingPositionBasedProgress = (duration == 0 || moveProgressByPosition == 1000);

      int moveProgressByTime = usingPositionBasedProgress ? 0 : 1000 - ((1000.0 * remainingTime ) / duration);
      int moveProgress = usingPositionBasedProgress ? moveProgressByPosition : moveProgressByTime;

      if (usingPositionBasedProgress && localDebug) {
        fprintf(stderr, "WARNING: NO DURATION AVAILABLE FOR RECALL COMMAND.\n");
      } else if (localDebug) {
        fprintf(stderr, "Axis %s START: %lf END: %lf CURRENT: %lf REMAINING: %lf\n"
                        "DURATION: %lf PROGRESS: %d\n",
                nameForAxis(axis),
                gAxisStartTime[axis], gAxisStartTime[axis] + duration,
                currentTime, remainingTime,
                duration, moveProgressByTime);
        fprintf(stderr, "Axis %s STARTPOS: %" PRId64 " ENDPOS: %" PRId64
                        " CURRENTPOS: %" PRId64 " REMAININGPOS: %" PRId64 "\n",
                nameForAxis(axis),
                startPosition, targetPosition,
                axisPosition, llabs(targetPosition - axisPosition));
      }

      int peakSpeed = usingPositionBasedProgress ? 1000 :
          peakSpeedForMove(axis, startPosition, targetPosition, duration);

      // In the middle part of the move, make sure we don't run behind or ahead too much.
      // We know when we plan to start slowing down, and that's a good enough goalpost.
      if ((!usingPositionBasedProgress) && moveProgressByTime >= kRampUpPeriodEnd &&
          moveProgressByTime <= kRampDownPeriodStart) {

          // When the slowdown begins at the 80% mark, it should be at 90% of the final position,
          // because it will move at, on average, half speed for 20% of the duration.
          int64_t moveDistanceBeforeStartOfSlowdown =
              moveDistanceFractionBeforeSlowdown * llabs(targetPosition - startPosition);

          // First some debugging data.
          double kFullSpeedPeriodByTime = kRampDownPeriodStart - kRampUpPeriodEnd;
          double kFullSpeedPeriodByDistance = kRampUpPeriodEnd;
          int expectedMoveProgressByPosition =
              // Progress during first kRampUpPeriodEnd positions
              50 +
              // Progress from 20 to 80 scaled to be from 10 to 90.
              ((moveProgressByTime - kRampUpPeriodEnd) * kFullSpeedPeriodByDistance / kFullSpeedPeriodByTime);

          if (localDebug) {
            fprintf(stderr, "Axis %s progressByTime: %d\n", nameForAxis(axis), moveProgress);
            fprintf(stderr, "Axis %s expected progress: %d actual: %d\n", nameForAxis(axis),
                    expectedMoveProgressByPosition, moveProgressByPosition);
            fprintf(stderr, "Axis %s peak speed before adjustment: %d\n", nameForAxis(axis), peakSpeed);
          }

          // Compute how long we have left to move at peak speed.
          double timeBeforeSlowingDown = MAX(remainingTime - (0.1 * duration), 0);
          if (localDebug) {
            fprintf(stderr, "Axis %s time before slowing down: %lf\n", nameForAxis(axis), timeBeforeSlowingDown);
          }

          // Update the peak speed.
          int64_t maxPPSForAxis = maximumPositionsPerSecondForAxis(axis);
          int64_t distanceMoved = llabs(axisPosition - startPosition);
          int64_t distanceLeft = moveDistanceBeforeStartOfSlowdown - distanceMoved;

          if (localDebug) {
            fprintf(stderr, "Axis %s max PPS for axis: %" PRId64" remaining distance: %" PRId64 "\n",
                    nameForAxis(axis), maxPPSForAxis, distanceLeft);
          }

          peakSpeed = (1000.0 * distanceLeft) / (maxPPSForAxis * timeBeforeSlowingDown);

          if (localDebug) {
            fprintf(stderr, "Axis %s peak speed after adjustment: %d\n", nameForAxis(axis), peakSpeed);
          }
      }
      bool creeping = (moveProgress == 1000) && (moveProgressByPosition < 1000);
#else
      bool usingPositionBasedProgress = true;
      int moveProgress = moveProgressByPosition;
      int peakSpeed = 1000;
      bool creeping = false;
#endif

      bool axisPositionHasChanged = (gAxisPreviousPosition[axis] != axisPosition);

      if (axisPositionHasChanged) {
        double deltaTimeSinceLastChange = currentTime - gAxisPreviousPositionTimestamp[axis];
        uint64_t deltaPositionSinceLastChange = axisPosition - gAxisPreviousPosition[axis];
        int64_t maxPPSForAxis = maximumPositionsPerSecondForAxis(axis);
        double targetPPS = peakSpeed * maxPPSForAxis / 1000.0;

        if (localDebug) {
          fprintf(stderr, "POSITION CHANGE: %" PRId64 "\n", deltaPositionSinceLastChange);
          fprintf(stderr, "TIME CHANGE: %lf\n", deltaTimeSinceLastChange);
          fprintf(stderr, "COMPUTED SPEED: %lf pps EXPECTED: %lf pps\n",
                  (double)((int)deltaPositionSinceLastChange / deltaTimeSinceLastChange),
                  targetPPS);
        }

        gAxisPreviousPosition[axis] = axisPosition;
        gAxisPreviousPositionTimestamp[axis] = currentTime;
      }


      if (moveProgress == 1000) {
        if (creeping) {
          // If we have reached the expected elapsed time but have not yet hit the target position,
          // creep as slowly as possible.  To do this, set the axis speed to +/-1 (the minimum nonzero
          // value).  The scaling function will increase that value as needed to ensure that the motor
          // does not stall.
          if (localDebug) {
            fprintf(stderr, "CREEPING TO FINAL POSITION AT MINIMUM SPEED\n");
          }
          setAxisSpeed(axis, 1 * direction, false);

          if (localDebug) {
            fprintf(stderr, "SPEEDINFO AXIS: %d POS: %04d TIME: %04d SPEED: %d (CREEP)\n",
                    axis, moveProgressByPosition, usingPositionBasedProgress ? -1 : moveProgress, 1 * direction);
          }
        } else {
          // If we have reached the target position, stop all motion on the axis.
          if (localDebug) {
            fprintf(stderr, "AXIS %d MOTION COMPLETE\n", axis);
          }
          gAxisMoveInProgress[axis] = false;
          setAxisSpeed(axis, 0, false);
          if (localDebug) {
            fprintf(stderr, "SPEEDINFO AXIS: %d POS: %04d TIME: %04d SPEED: %d (stopped)\n",
                    axis, moveProgressByPosition, usingPositionBasedProgress ? -1 : moveProgress, 0 * direction);
          }
        }
      } else {
        // Compute the target speed based on the current move progress.
        //
        // If we do not have calibration data, specify a floor (MIN_PAN_TILT_SPEED) to
        // ensure that the motion does not get stuck.  The reason for this is because
        // the progress (and thus the speed) increases as the encoder position
        // increases, so if the motor stalls (no motion) at a given voltage, the speed
        // would never increase, so the motor would never start moving.
        //
        // Do not use a minimum speed if we are using wall clock time, because computed
        // progress doesn't depend on the motors changing the encoder position.
        int speed = usingPositionBasedProgress ?
            MAX(computeSpeed(moveProgress, peakSpeed), MIN_PAN_TILT_SPEED) :
            computeSpeed(moveProgress, peakSpeed);

        if (axisPositionHasChanged || usingPositionBasedProgress || moveProgress < kRampUpPeriodEnd ||
            moveProgress > kRampDownPeriodStart || gAxisLastMoveSpeed[axis] == 0) {
          setAxisSpeed(axis, speed * direction, localDebug);
        } else if (localDebug) {
          fprintf(stderr, "Skipping speed change to avoid feedback drift because axis position has not changed.\n");
        }
        if (localDebug) {
            fprintf(stderr, "SPEEDINFO AXIS: %d POS: %04d TIME: %04d SPEED: %d (%s)\n",
                    axis, moveProgressByPosition, usingPositionBasedProgress ? -1 : moveProgress, speed * direction,
                    (moveProgress < kRampUpPeriodEnd) ? "RAMP UP" :
                        (moveProgress > kRampDownPeriodStart) ? "RAMP DOWN" : "NORMAL");
            fprintf(stderr, "AXIS %d SPEED NOW %d * %d (%d) at %d\n",
                    axis, speed, direction, speed * direction, moveProgress);
        }
      }
    } else if (localDebug > 1) {
      fprintf(stderr, "NOT UPDATING AXIS %d: NOT IN MOTION\n", axis);
    }
  }
}

// Returns the integer value that is at least as far from zero as
// the specified double value, but without losing the sign.
//
// In other words for positive numbers, this returns an integer that
// is at least as large as the original value, and for negative
// numbers, this returns an integer that is at least as large a
// negative value.
//
// Or, put another way, this returns the ceiling of the absolute
// value of the number, but with the original value's sign.
double absceil(double value) {
  return (value > 0) ? ceil(value) : floor(value);
}

// Scales a speed from a scale of 0..fromScale to 0..toScale.
//
// If scaleData is NULL, 0 is 0, and all other input values map onto
// equally sized groups of numbers on the output size or input groups onto
// single output values, depending on direction.
//
// If scaleData is non-NULL, it is assumed to be a set of values
// returned by a call to the convertSpeedValues function on the
// raw calibration results for a given axis.
//
// Thus, each value represents the core scale value that most closely
// approximates that speed in the target scale.  Any zero-speed values
// are skipped and replaced by the first nonzero value.  This isn't
// exactly right mathematically, but it is as close as is physically
// possible given motors' tendency to stall out at low speeds.
// If fromScale is not the core scale, the value is first converted
// to that scale.
int scaleSpeed(int speed, int fromScale, int toScale, int32_t *scaleData) {
  bool localDebug = false;

  if (scaleData == NULL) {
    return absceil((speed * 1.0 * toScale) / fromScale);
  }

  // Fast path for no motion.
  if (speed == 0) {
    return 0;
  }

  int absSpeed = abs(speed);
  int sign = (speed < 0) ? -1 : 1;

  // If the input scale is anything other than SCALE_CORE,
  // first convert it to that scale so that it is in the
  // same scale as scaleData.
  if (fromScale != SCALE_CORE) {
    absSpeed = scaleSpeed(absSpeed, fromScale, SCALE_CORE, NULL);
  }

  // Find the lowest speed that is at least as large as the
  // target speed.
  for (int i = 0; i <= toScale; i++) {
    if (scaleData[i] == absSpeed) {
      int retval = i * sign;
      if (localDebug) {
        fprintf(stderr, "SCALED %d to %d.  ERROR: %d (%lf%%)\n",
                speed, retval, abs(absSpeed - scaleData[i]),
                100 * fabs(absSpeed - scaleData[i]) / absSpeed);
      }
      return retval;
    } else if (scaleData[i] > absSpeed) {
      // The native speed at `i` is the smallest speed
      // greater than the target core speed.  If the
      // native speed that's one slower than this speed
      // provides no motion, return native speed `i`
      // because we never want to stop the motor for
      // a nonzero value.  (It is the responsibility of
      // the VISCA sender/controller to not send out
      // junk values if the stick is near the center,
      // not the responsiblity of the VISCA interpreter/
      // motor controller.)
      if (scaleData[i - 1] == 0) {
        int retval = i * sign;
        if (localDebug) {
          fprintf(stderr, "SCALED %d to %d.  ERROR: %d (%lf%%)\n",
                  speed, retval, abs(absSpeed - scaleData[i]),
                  100 * fabs(absSpeed - scaleData[i]) / absSpeed);
        }
        return retval;
      }
      // Both the current native speed value and the
      // native speed value below this one are nonzero.
      // Return i or i-1, whichever is closer, but not
      // if the speed value at i-1 is zero.
      int distanceBelow = absSpeed - scaleData[i-1];
      int distanceAbove = scaleData[i] - absSpeed;
      int chosenIndex = ((distanceAbove < distanceBelow) ? i : i-1);
      int retval = chosenIndex * sign;
      if (localDebug) {
        fprintf(stderr, "SCALED %d to %d.  ERROR: %d (%lf%%)\n",
                speed, retval, abs(absSpeed - scaleData[chosenIndex]),
                100 * fabs(absSpeed - scaleData[chosenIndex]) / absSpeed);
      }
      return retval;
    }
  }
  // If we ran off the end, return the maximum speed.
  int chosenIndex = toScale;
  int retval = chosenIndex * sign;
  if (localDebug) {
    fprintf(stderr, "SCALED %d to %d.  ERROR: %d (%lf%%)\n",
            speed, retval, abs(absSpeed - scaleData[chosenIndex]),
            100 * fabs(absSpeed - scaleData[chosenIndex]) / absSpeed);
  }
  return retval;
}

int32_t *convertSpeedValues(int64_t *speedValues, int maxSpeed, axis_identifier_t axis) {
  assert(maxSpeed > 0);

  // Compute the size of the last scale step between the fastest
  // value and the slowest value and add that to the maximum value
  // so that we slightly stretch the number of values that map
  // onto a core speed of 1,000.
  int64_t last_scale_step_size =
      speedValues[maxSpeed] - speedValues[maxSpeed - 1];
  int64_t scale_max =
      speedValues[maxSpeed] + (last_scale_step_size / 2);
  if (scale_max == 0) {
      fprintf(stderr, "Defective speed data detected for axis %d.  Using fake numbers.\n", axis);
      fprintf(stderr, "You should recalibrate immediately\n");
      scale_max = maxSpeed;
  }
  int32_t *outputValues =
      (int32_t *)malloc((maxSpeed + 1) * sizeof(int32_t));
  for (int i = 0; i <= maxSpeed; i++) {
      outputValues[i] = speedValues[i] * 1000 / scale_max;
  }
  return outputValues;
}

int64_t scaleVISCAPanTiltSpeedToCoreSpeed(int speed) {
  return scaleSpeed(speed, PAN_SCALE_VISCA, SCALE_CORE, NULL);
}

int64_t scaleVISCAZoomSpeedToCoreSpeed(int speed) {
  if (gVISCAUsesCoreScale) {
    return speed;
  }
  int64_t retval = scaleSpeed(speed, ZOOM_SCALE_VISCA, SCALE_CORE, NULL);
  return retval;
}

int64_t getAxisPosition(axis_identifier_t axis) {
  switch(axis) {
    case axis_identifier_pan:
    case axis_identifier_tilt:
    {
        int64_t panPosition, tiltPosition;
#if PAN_AND_TILT_POSITION_SUPPORTED
        if (GET_PAN_TILT_POSITION(&panPosition, &tiltPosition)) {
            return (axis == axis_identifier_pan) ? panPosition : tiltPosition;
        }
#endif
        break;
    }
    case axis_identifier_zoom:
#if ZOOM_POSITION_SUPPORTED
        return GET_ZOOM_POSITION();
#else
        break;
#endif
  }
  return 0;
}

bool setZoomPosition(int64_t position, int64_t speed, double duration) {
    bool localDebug = false;

    if (localDebug) {
      fprintf(stderr, "@@@ setZoomPosition: %" PRId64 " Speed: %" PRId64 "\n", position, speed);
    }

    if (duration == 0) {
        duration = durationForMove(kFlagMoveZoom, kUnusedPosition, kUnusedPosition, position);
    }

    return SET_ZOOM_POSITION(position, speed, makeDurationValid(axis_identifier_zoom, duration, position));
}

bool setPanTiltPosition(int64_t panPosition, int64_t panSpeed,
                        int64_t tiltPosition, int64_t tiltSpeed, double duration) {
    bool localDebug = false;

    if (localDebug) {
      if (duration == 0) {
        fprintf(stderr, "WARNING: NO DURATION AVAILABLE FOR PAN/TILT SET COMMAND.\n");
      }

      fprintf(stderr, "@@@ setPanTiltPosition: Pan position %" PRId64 " speed %" PRId64 "\n"
                      "                        Tilt position %" PRId64 " speed %" PRId64 "\n",
                      panPosition, panSpeed, tiltPosition, tiltSpeed);
    }

    if (!absolutePositioningSupportedForAxis(axis_identifier_pan) ||
        !absolutePositioningSupportedForAxis(axis_identifier_tilt)) {
        fprintf(stderr, "Pan/tilt absolute positioning unsupported.");
        return false;
    }

    if (duration == 0) {
        duration = durationForMove(kFlagMovePan | kFlagMoveTilt, panPosition, tiltPosition, kUnusedPosition);
    }

    return SET_PAN_TILT_POSITION(panPosition, panSpeed, tiltPosition, tiltSpeed,
                                 makeDurationValid(axis_identifier_pan, duration, panPosition),
                                 makeDurationValid(axis_identifier_tilt, duration, tiltPosition));
}

double makeDurationValid(axis_identifier_t axis, double duration, int64_t position) {
  bool localDebug = false;
  if (localDebug) {
    fprintf(stderr, "makeDurationValid(axis %d, duration %lf, position: %lld)\n",
      axis, duration, (long long)position);
    int64_t currentPosition = getAxisPosition(axis);
    fprintf(stderr, "    Current position: %lld for %s\n", currentPosition,
            nameForAxis(axis));
  }
  double slowestDuration = slowestMoveForAxisToPosition(axis, position);
  if (duration > slowestDuration) {
    duration = slowestDuration;
    if (localDebug) {
      fprintf(stderr, "Axis %s clamped to %lf (slowest duration)\n", nameForAxis(axis),
              slowestDuration);
    }
  }
  double fastestDuration = fastestMoveForAxisToPosition(axis, position);
  if (duration < fastestDuration) {
    duration = fastestDuration;
    if (localDebug) {
      fprintf(stderr, "Axis %s clamped to %lf (fastest duration)\n", nameForAxis(axis),
               fastestDuration);
    }
  }
  if (localDebug) {
    fprintf(stderr, "Axis %s final value: %lf\n", nameForAxis(axis), duration);
  }
  return duration;
}

/**
 * Computes the move duration for a move to panPosition/tiltPosition/zoomPosition or some subset
 * thereof (controlled by flags).
 *
 * By default, this returns the value of currentRecallTime(), but only if every axis can reach the
 * target position that quickly or slowly.
 *
 * If it is not possible to make all axes reach the destination simultaneously because of speed
 * differences, this returns the longer of the possible durations, and one axis will just finish
 * sooner.
 *
 * Additionally, this caps the maximum amount of change at +/- 25% to prevent a massively slow or
 * fast axis (or a very short move) from changing things too severely.
 */
double durationForMove(moveModeFlags flags, int64_t panPosition, int64_t tiltPosition, int64_t zoomPosition) {
  bool localDebug = false;

  double recallTime = currentRecallTime();
  double originalRecallTime = recallTime;

  if (localDebug) {
    fprintf(stderr, "Default duration for move: %lf\n", recallTime);
  }

  // First, make sure the ideal duration is not too fast for any axis.
  if (flags & kFlagMovePan) {
    double timeAtMaximumSpeed = fastestMoveForAxisToPosition(axis_identifier_pan, panPosition);
    if (timeAtMaximumSpeed == 0) {
      if (localDebug) {
        fprintf(stderr, "Maximum speed for pan axis not available.  Ignoring axis.\n");
      }
    } else {
      recallTime = MAX(recallTime, timeAtMaximumSpeed);
      if (localDebug) {
        fprintf(stderr, "Fastest duration for move (pan): %lf.  Now %lf.\n", timeAtMaximumSpeed,
                recallTime);
      }
    }
  }
  if (flags & kFlagMoveTilt) {
    double timeAtMaximumSpeed = fastestMoveForAxisToPosition(axis_identifier_tilt, tiltPosition);
    if (timeAtMaximumSpeed == 0) {
      if (localDebug) {
        fprintf(stderr, "Maximum speed for tilt axis not available.  Ignoring axis.\n");
      }
    } else {
      recallTime = MAX(recallTime, timeAtMaximumSpeed);
      if (localDebug) {
        fprintf(stderr, "Fastest duration for move (tilt): %lf.  Now %lf.\n", timeAtMaximumSpeed,
                recallTime);
      }
    }
  }
  if (flags & kFlagMoveZoom) {
    double timeAtMaximumSpeed = fastestMoveForAxisToPosition(axis_identifier_zoom, zoomPosition);
    if (timeAtMaximumSpeed == 0) {
      if (localDebug) {
        fprintf(stderr, "Maximum speed for zoom axis not available.  Ignoring axis.\n");
      }
    } else {
      recallTime = MAX(recallTime, timeAtMaximumSpeed);
      if (localDebug) {
        fprintf(stderr, "Fastest duration for move (zoom): %lf.  Now %lf.\n", timeAtMaximumSpeed,
                recallTime);
      }
    }
  }

  // Now, make sure the ideal duration is not too slow for any axis.
  if (flags & kFlagMovePan) {
    double timeAtMinimumSpeed = slowestMoveForAxisToPosition(axis_identifier_pan, panPosition);
    if (timeAtMinimumSpeed == 0) {
      if (localDebug) {
        fprintf(stderr, "Minimum speed for pan axis not available.  Ignoring axis.\n");
      }
    } else {
      recallTime = MIN(recallTime, timeAtMinimumSpeed);
      if (localDebug) {
        fprintf(stderr, "Slowest duration for move (pan): %lf.  Now %lf.\n", timeAtMinimumSpeed,
                recallTime);
      }
    }
  }
  if (flags & kFlagMoveTilt) {
    double timeAtMinimumSpeed = slowestMoveForAxisToPosition(axis_identifier_tilt, tiltPosition);
    if (timeAtMinimumSpeed == 0) {
      if (localDebug) {
        fprintf(stderr, "Minimum speed for tilt axis not available.  Ignoring axis.\n");
      }
    } else {
      recallTime = MIN(recallTime, timeAtMinimumSpeed);
      if (localDebug) {
        fprintf(stderr, "Slowest duration for move (tilt): %lf.  Now %lf.\n", timeAtMinimumSpeed,
                recallTime);
      }
    }
  }
  if (flags & kFlagMoveZoom) {
    double timeAtMinimumSpeed = slowestMoveForAxisToPosition(axis_identifier_zoom, zoomPosition);
    if (timeAtMinimumSpeed == 0) {
      if (localDebug) {
        fprintf(stderr, "Minimum speed for zoom axis not available.  Ignoring axis.\n");
      }
    } else {
      recallTime = MIN(recallTime, timeAtMinimumSpeed);
      if (localDebug) {
        fprintf(stderr, "Slowest duration for move (zoom): %lf.  Now %lf.\n", timeAtMinimumSpeed,
                recallTime);
      }
    }
  }

  if (recallTime > (1.25 * originalRecallTime) ||
      recallTime < (.75 * originalRecallTime)) {
    if (localDebug) {
      fprintf(stderr, "Clamped time too extreme.  Returning unclamped time %lf.\n",
              originalRecallTime);
    }
    return originalRecallTime;
  }

  return recallTime;
}

int64_t minimumPositionsPerSecondForAxis(axis_identifier_t axis) {
  switch(axis) {
    case axis_identifier_pan:
        return MIN_PAN_POSITIONS_PER_SECOND();
    case axis_identifier_tilt:
        return MIN_TILT_POSITIONS_PER_SECOND();
    case axis_identifier_zoom:
        return MIN_ZOOM_POSITIONS_PER_SECOND();
  }
  return 0;
}

int64_t minimumPositionsPerSecondForData(int64_t *calibrationData, int maximumSpeed) {
  if (calibrationData == NULL) {
    return 0;
  }
  for (int i = 0; i <= maximumSpeed; i++) {
    if (calibrationData[i] != 0) {
      return calibrationData[i];
    }
  }
  return 0;
}

int64_t maximumPositionsPerSecondForAxis(axis_identifier_t axis) {
  switch(axis) {
    case axis_identifier_pan:
        return MAX_PAN_POSITIONS_PER_SECOND();
    case axis_identifier_tilt:
        return MAX_TILT_POSITIONS_PER_SECOND();
    case axis_identifier_zoom:
        return MAX_ZOOM_POSITIONS_PER_SECOND();
  }
  return 0;
}

// Returns the maximum core speed that the motor should move once the initial ramp-up period
// is over, and prior to the ramp-down period beginning.
//
// Per the explanation in the comments for computeSpeed(), we can compute this by determining
// the average speed and dividing by a value halfway between the total move time and the
// time spent at full speed (because the ramp periods average 50% speed).
//
// We start by computing the number of positions per second given the time and distance.
// We then divide by moveTimeFractionBeforeSlowdown.
int peakSpeedForMove(axis_identifier_t axis, int64_t fromPosition, int64_t toPosition, double time) {
  bool localDebug = false;
  double peakPositionsPerSecond = (llabs(fromPosition - toPosition) / time) / moveTimeFractionBeforeSlowdown;

  if (localDebug) {
    fprintf(stderr, "Peak points per second for move along axis %d from %lld to %lld over time %lf is %lf\n",
            axis, fromPosition, toPosition, time, peakPositionsPerSecond);
  }

  int64_t maxPPSForAxis = maximumPositionsPerSecondForAxis(axis);
  int peakSpeed = (peakPositionsPerSecond * 1000) / maxPPSForAxis;

  if (localDebug) {
    fprintf(stderr, "Peak speed: %d (max PPS for axis is %" PRId64 ", peakPPS is %lf)\n",
            peakSpeed, maxPPSForAxis, peakPositionsPerSecond);
  }

  return peakSpeed;
}

double fastestMoveForAxisToPosition(axis_identifier_t axis, int64_t position) {
  // With 80% of the move at 100% speed and the first and last 10% transitioning from/to 0% and
  // averaging 50%, that means the average speed through the entire move is always 90% of the
  // maximum speed from that middle 80%.  This is a slight approximation because there is a
  // minimum speed for the motors, but we ignore that to make the computation reasonable.
  //
  // Based on that, we can take the maximum number of positions per second that the axis is
  // capable of moving at maximum speed, mutiply times moveTimeFractionBeforeSlowdown, and
  // get the maximum average speed.
  //
  // You can then divide the total move distance by that number, and that gives you the
  // minimum number of seconds that the camera can spend reaching that destination (using
  // our s-curve algorithm; it is, of course, possible to achieve a slightly shorter
  // duration by ramping more quickly to the maximum speed).

  int64_t maximumPositionsPerSecond = maximumPositionsPerSecondForAxis(axis);
  if (maximumPositionsPerSecond == 0) {
    return 0;
  }
  int64_t currentPosition = getAxisPosition(axis);
  int64_t distance = llabs(position - currentPosition);
  return (double)distance / ((double)maximumPositionsPerSecond * moveTimeFractionBeforeSlowdown);
}

double slowestMoveForAxisToPosition(axis_identifier_t axis, int64_t position) {
  // This computes the maximum possible duration that the axis can spend reaching a
  // given position by dividing the number of positions by the number of positions
  // per second at the slowest native speed.
  //
  // As with the fastest move computation, we ignore any ramp time to the slowest
  // speed, both because it would be too hard to compute that ramp time, and
  // because in practice, the ramp time to the slowest native speed is usually
  // negligible anyway.

  int64_t minimumPositionsPerSecond = minimumPositionsPerSecondForAxis(axis);
  if (minimumPositionsPerSecond == 0) {
    return 0;
  }
  int64_t currentPosition = getAxisPosition(axis);
  int64_t distance = llabs(position - currentPosition);
  return (double)distance / (double)minimumPositionsPerSecond;
}

bool setAxisSpeed(axis_identifier_t axis, int64_t coreSpeed, bool debug) {
  return setAxisSpeedInternal(axis, coreSpeed, debug, false);
}

bool setAxisSpeedRaw(axis_identifier_t axis, int64_t speed, bool debug) {
  return setAxisSpeedInternal(axis, speed, debug, true);
}

bool setAxisSpeedInternal(axis_identifier_t axis, int64_t speed, bool debug, bool isRaw) {
  if (debug) {
    if (gAxisLastMoveSpeed[axis] != speed) {
      fprintf(stderr, "CHANGED AXIS %d from %" PRId64 " to %" PRId64 "\n", axis, gAxisLastMoveSpeed[axis], speed);
    }
  }

  int panReversed = panMotorReversed() ? -1 : 1;
  int tiltReversed = tiltMotorReversed() ? -1 : 1;
  int zoomReversed = zoomMotorReversed() ? -1 : 1;

  if (debug) {
    fprintf(stderr, "Reverse motor direction for pan: %s tilt: %s zoom: %s\n",
            panReversed ? "YES" : "NO", tiltReversed ? "YES" : "NO", zoomReversed ? "YES" : "NO");
  }

  gAxisLastMoveSpeed[axis] = speed;

  switch(axis) {
    case axis_identifier_pan:
        return SET_PAN_TILT_SPEED(speed * panReversed, gAxisLastMoveSpeed[axis_identifier_tilt] * tiltReversed, isRaw);
    case axis_identifier_tilt:
        return SET_PAN_TILT_SPEED(gAxisLastMoveSpeed[axis_identifier_pan] * panReversed, speed * tiltReversed, isRaw);
    case axis_identifier_zoom:
        return SET_ZOOM_SPEED(speed * zoomReversed, isRaw);
  }
  return false;
}

bool absolutePositioningSupportedForAxis(axis_identifier_t axis) {
  switch(axis) {
    case axis_identifier_pan:
      return PAN_AND_TILT_POSITION_SUPPORTED;
    case axis_identifier_tilt:
      return PAN_AND_TILT_POSITION_SUPPORTED;
    case axis_identifier_zoom:
      return ZOOM_POSITION_SUPPORTED;
  }
  return false;
}

void cancelRecallIfNeeded(char *context) {
  bool didCancel = false;
  for (axis_identifier_t axis = axis_identifier_pan; axis < NUM_AXES; axis++) {
    if (gAxisMoveInProgress[axis]) {
      didCancel = true;
    }
    gAxisMoveInProgress[axis] = false;
  }
  if (didCancel) {
    fprintf(stderr, "RECALL CANCELLED (%s)\n", context);
  }
}

bool setAxisPositionIncrementally(axis_identifier_t axis, int64_t position, int64_t maxSpeed, double duration) {
  bool localDebug = false;

  if (localDebug) {
    fprintf(stderr, "setAxisPositionIncrementally for axis %d\n", axis);
  }
  if (!absolutePositioningSupportedForAxis(axis)) {
    fprintf(stderr, "Absolute positioning not supported.\n");
    return false;
  }

  if (duration == 0 && localDebug) {
    fprintf(stderr, "WARNING: NO DURATION AVAILABLE FOR INCREMENTAL SET COMMAND.\n");
  } else if (localDebug) {
    fprintf(stderr, "DURATION COMPUTED AS: %lf\n", duration);
  }

  gAxisMoveInProgress[axis] = true;
  gAxisStartTime[axis] = timeStamp();
  gAxisDuration[axis] = duration;
  gAxisMoveStartPosition[axis] = getAxisPosition(axis);
  gAxisMoveTargetPosition[axis] = position;
  gAxisMoveMaxSpeed[axis] = maxSpeed;
  gAxisPreviousPosition[axis] = gAxisMoveStartPosition[axis];

  if (localDebug) {
    fprintf(stderr, "gAxisMoveInProgress[%d] = %s\n", axis, gAxisMoveInProgress[axis] ? "true" : "false");
    fprintf(stderr, "gAxisMoveStartPosition[%d] = %" PRId64 "\n", axis, gAxisMoveStartPosition[axis]);
    fprintf(stderr, "gAxisMoveTargetPosition[%d] = %" PRId64 "\n", axis, gAxisMoveTargetPosition[axis]);
    fprintf(stderr, "gAxisMoveMaxSpeed[%d] = %" PRId64 "\n", axis, gAxisMoveMaxSpeed[axis]);
  }
  return true;
}

#pragma mark - Networking


void *runNetworkThread(void *argIgnored) {
  // VISCA-over-IP can be run on any port, ostensibly, but most cameras
  // use UDP port 52381, so this uses that port as well.
  unsigned short port = 52381;

  struct sockaddr_in client, server;
  socklen_t structLength;

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  memset((char *) &client, 0, sizeof(client));
  client.sin_family = AF_INET;
  client.sin_addr.s_addr = htonl(INADDR_ANY);
  client.sin_port = htons(port);

  memset((char *) &server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(port);

  structLength = sizeof(server);
  if (bind(sock, (struct sockaddr *) &server, structLength) < 0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  while (1) {
    /* Get an increment request */
    structLength = sizeof(client);
    visca_cmd_t command;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;  // Update the speed of automatic recall operations 100x per second (10 ms).

    if (select(sock + 1, &read_fds, NULL, NULL, &timeout) > 0) {
      int bytes_received = recvfrom(sock, &command, sizeof(visca_cmd_t), 0,
        (struct sockaddr *) &client, &structLength);

      if (bytes_received < 0) {
        perror("recvfrom");
      } else if (structLength > 0) {
        bool success = handleVISCAPacket(command, sock, (struct sockaddr *)&client, structLength);
        if (!success) {
          fprintf(stderr, "VISCA error\n");
          while (!sendVISCAResponse(failedVISCAResponse(), command.sequence_number, sock, (struct sockaddr *)&client, structLength));
        }
      }
    }
    // Read data or timeout.
    handleRecallUpdates();
  }

  return NULL;
}

bool sendVISCAResponse(visca_response_t *response, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength) {
  uint16_t length = htons(response->len);
  response->sequence_number = sequenceNumber;
  if (length > 0) {
    length += 8;  // 8-byte UDP header.
    if (sendto(sock, response, length, 0, client, structLength) < 0) {
      perror("sendto");
      return false;
    }
  }
  return true;
}


#pragma mark - VISCA command translation

void printbuf(uint8_t *buf, int len) {
  fprintf(stderr, "BUF: ");
  for (int i = 0; i < len; i++) {
    fprintf(stderr, " %02x", buf[i]);
  }
  fprintf(stderr, "\n");
}

bool handleVISCAPacket(visca_cmd_t command, int sock, struct sockaddr *client, socklen_t structLength) {
  if (debug_verbose) fprintf(stderr, "GOT VISCA PACKET %s\n", VISCAMessageDebugString(command.data, htons(command.len)));

  if (command.cmd[0] != 0x1) {
    fprintf(stderr, "INVALID[0 = %02x]\n", command.cmd[0]);
    return false;
  }
  if (command.cmd[1] == 0x10) {
    return handleVISCAInquiry(command.data, htons(command.len), command.sequence_number, sock, client, structLength);
  } else {
    return handleVISCACommand(command.data, htons(command.len), command.sequence_number, sock, client, structLength);
  }
}

visca_response_t *failedVISCAResponse(void) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x60, 0x20, 0xff };
  SET_RESPONSE(&response, data);
  return &response;
}

visca_response_t *enqueuedVISCAResponse(void) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x41, 0xff };
  SET_RESPONSE(&response, data);
  return &response;
}

visca_response_t *completedVISCAResponse(void) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x41, 0xff };
  SET_RESPONSE(&response, data);
  return &response;
}

visca_response_t *tallyEnabledResponse(int tallyState) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x50, 0x00, 0xff };
  data[2] = tallyState;
  SET_RESPONSE(&response, data);
  return &response;
}

visca_response_t *tallyModeResponse(int tallyState) {
  static visca_response_t response;
  uint8_t data[] = { 0x10, 0x50, 0x00, 0xff };
  data[2] = tallyState;
  SET_RESPONSE(&response, data);
  return &response;
}

bool handleVISCAInquiry(uint8_t *command, uint8_t len, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength) {
  if (debug_verbose) fprintf(stderr, "GOT VISCA INQUIRY %s\n", VISCAMessageDebugString(command, len));

  // All VISCA inquiries start with 0x90.
  if(command[0] != 0x81) return false;

  switch(command[1]) {
    case 0x09:
      switch(command[2]) {
        case 0x04:
          if (command[3] == 0x07 && command[4] == 0xFF) {
            while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
            static visca_response_t response;
            uint8_t data[] = {
                0x10, 0x50, 0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed, 0xba, 0xbe,
                (SCALE_CORE >> 8) & 0xff, SCALE_CORE & 0xff, 0xff
            };
            SET_RESPONSE(&response, data);
            while (!sendVISCAResponse(&response, sequenceNumber, sock, client, structLength));

            gVISCAUsesCoreScale = true;
            return true;
          }
        case 0x7e:
          if (command[3] == 0x01 && command[4] == 0x0a) {
            while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
            int tallyState = GET_TALLY_STATE();
            visca_response_t *response = NULL;
            if (command[5] == 0x01 && command[6] == 0xFF) {
              // 8x 09 7E 01 0A 01 FF -> y0 50 0p FF
              response = tallyEnabledResponse(tallyState);
            } else if (command[5] == 0xFF) {
              // 8x 09 7E 01 0A FF -> y0 50 0p FF
              response = tallyModeResponse(tallyState);
            } else {
              fprintf(stderr, "Unknown tally request\n");
              break;
            }
            while (!sendVISCAResponse(response, sequenceNumber, sock, client, structLength));
            return true;
          }
          break;
        default:
          break;
      }
    default:
      break;
  }

  // Zoom position inquiry: 8x 09 04 47 FF -> y0 50 0p 0q 0r 0s FF ; pqrs -> 0x0000.0x40000

  return false;
}

tallyState VISCA_getTallyState(void) {
  return gTallyState;
}

bool setTallyOff(void) {
  gTallyState = kTallyStateOff;
#ifdef SET_TALLY_STATE
  return SET_TALLY_STATE(kTallyStateOff);
#elif USE_VISCA_TALLY_SOURCE
  return true;
#else
  return false;
#endif
}

bool setTallyRed(void) {
  gTallyState = kTallyStateRed;
#ifdef SET_TALLY_STATE
  return SET_TALLY_STATE(kTallyStateRed);
#elif USE_VISCA_TALLY_SOURCE
  return true;
#else
  return false;
#endif
}

bool setTallyGreen(void) {
  gTallyState = kTallyStateGreen;;
#ifdef SET_TALLY_STATE
  return SET_TALLY_STATE(kTallyStateGreen);
#elif USE_VISCA_TALLY_SOURCE
  return true;
#else
  return false;
#endif
}

const char *tallyStateName(tallyState state) {
  switch(state) {
    case kTallyStateOff:
      return "kTallyStateOff";
    case kTallyStateUnknown1:
      return "kTallyStateUnknown1";
    case kTallyStateUnknown2:
      return "kTallyStateUnknown2";
    case kTallyStateUnknown3:
      return "kTallyStateUnknown3";
    case kTallyStateUnknown4:
      return "kTallyStateUnknown4";
    case kTallyStateRed:
      return "kTallyStateRed";
    case kTallyStateGreen:
      return "kTallyStateGreen";
    default:
      return "UNKNOWN STATE";
  }
}

void setResponseArray(visca_response_t *response, uint8_t *array, uint8_t count) {
  response->cmd[0] = 0x01;
  response->cmd[1] = 0x11;
  response->len = htons(count);
  bcopy(array, response->data, count);
}

char *VISCAMessageDebugString(uint8_t *command, uint8_t len) {
  static char *buf = NULL;
  if (buf != NULL) {
    free(buf);
    buf = NULL;
  }

  for (int i = 0; i < len; i++) {
    char *oldbuf = buf;
    asprintf(&buf, "%s%s0x%02x", (buf == NULL) ? "" : buf, (buf == NULL) ? "" : " ", command[i]);
    if (oldbuf) free(oldbuf);
  }
  return buf;
}

bool handleVISCACommand(uint8_t *command, uint8_t len, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength) {

  if (gCalibrationModeVISCADisabled) {
    return false;
  }

  if (debug_verbose) fprintf(stderr, "GOT VISCA COMMAND %s\n", VISCAMessageDebugString(command, len));


  // All VISCA commands start with 0x8x, where x is the camera number.  For IP, always 1.
  if(command[0] != 0x81) return false;

  switch(command[1]) {
    case 0x01:
      switch(command[2]) {
        case 0x04:
            switch(command[3]) {
              case 0x00: // Power on (2) / off (3): Not implemented.
                break;
              case 0x03: // Manual red gain: Not implemented.  02=reset, 02=up, 03=down,
                         // 00-00-0p-0q = set to pq (0x00..0x80)
                break;
              case 0x07: // Zoom stop (00), tele (02 or 20-27), wide (03 or 30-37)
              {
                while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
                uint8_t zoomCmd = command[4];
                if (zoomCmd == 2) zoomCmd = 0x23;
                if (zoomCmd == 3) zoomCmd = 0x33;

                uint32_t zoomSpeed = 0;
                if (gVISCAUsesCoreScale && (zoomCmd == 0x2f || zoomCmd == 0x3f)) {
                    // Nonstandard zoom command.  Supported only after asking for the maximum
                    // allowable zoom speed.

                    zoomSpeed = (command[5] << 8) | command[6];

                    if (zoomCmd == 0x3f) {
                      zoomSpeed = -zoomSpeed;
                    }
fprintf(stderr, "Speed: %d\n", zoomSpeed);

                } else {
                    if (zoomCmd != 0) {  // Leave the speed at zero if the command is "zoom stop".
                        int8_t zoomRawSpeed = command[4] & 0xf;

                        // VISCA zoom speeds go from 0 to 7, but the output speed's 0 is stopped, so
                        // immediately convert the range to be from 1 to 8 instead.
                        zoomSpeed = ((zoomCmd & 0xf0) == 0x20) ? zoomRawSpeed + 1 : - (zoomRawSpeed + 1);
                    }
                }
                // If there is a move (recall or position set) in progress, ignore any
                // requests to set the zoom speed to zero, because that means the
                // operator is not touching the stick.  But if the operator touches the
                // stick, abort any in-progress move immediately.
                if (!moveInProgress() || zoomSpeed != 0) {
                    cancelRecallIfNeeded("Zoom command received");
                    setAxisSpeed(axis_identifier_zoom, scaleVISCAZoomSpeedToCoreSpeed(zoomSpeed), false);
                }
                while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                return true;
              }
              case 0x08: // Focus: Not implemented
                break;
              case 0x0B: // Iris settings.
                break;
              case 0x10: // One-push white balance trigger: Not implemented.  Next byte 05.
                break;
              case 0x19: // Lens initialization start: Not implemented.
                break;
              case 0x2B: // Iris settings.
                break;
              case 0x2F: // Iris settings.
                break;
              case 0x35: // White balance set: Not implemented.
                         // cmd4=00: auto, 01=in, 02=out, 03=one-push, 04=auto tracing,
                         // 05=manual, 0C=sodium.
                break;
              case 0x38: // Focus or Curve Tracking: Not implemented.
                break;
              case 0x39: // AE settings.
                break;
              case 0x3C: // Flickerless settings.
                break;
              case 0x3F: // Store/restore presets.
                 // Do not attempt to use absolute positioning while in calibration mode!
                 if (gCalibrationMode) {
                     fprintf(stderr, "Ignoring preset store/recall while in calibration mode.\n");
                     return false;
                 }

                if (command[6] == 0xFF) {
                  int presetNumber = command[5];
                  switch(command[4]) {
                    case 0:
                      // Reset: not implemented;
                      break;
                    case 1:
                      if (savePreset(presetNumber)) {
                          while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                          return true;
                      }
                    case 2:
                      if (recallPreset(presetNumber)) {
                          while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                          return true;
                      }
                  }
                }
                break;
              case 0x47: // Absolute zoom.
#if ZOOM_POSITION_SUPPORTED
              {
                 // Do not attempt to use absolute positioning while in calibration mode!
                 if (gCalibrationMode) {
                     fprintf(stderr, "Ignoring absolute zoom while in calibration mode.\n");
                     return false;
                 }

                while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
                uint32_t position = ((command[4] & 0xf) << 12) | ((command[5] & 0xf) << 8) |
                                    ((command[6] & 0xf) << 4) | (command[7] & 0xf);
                // VISCA zoom speeds go from 0 to 7, but we need to treat 0 as stopped, so immediately
                // convert that to be from 1 to 8.  But only do that if it is a command that has a speed
                // parameter.  Otherwise go with a default based on whether the camera is live or not.
                uint8_t speed = ((command[len - 2] & 0xf0) == 0) ? ((command[8] & 0xf) + 1) :
                    getVISCAZoomSpeedFromTallyState();
                cancelRecallIfNeeded("setZoomPosition");
                setZoomPosition(position, scaleVISCAZoomSpeedToCoreSpeed(speed), 0);

                // If (command[9] & 0xf0) == 0, then the low bytes of 9-12 are focus position,
                // and speed is at position 13, shared with focus.  If we ever add support for
                // focusing, handle that case here.

                while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                return true;
              }
#else
                // Absolute zoom position unsupported.
                break;
#endif
              case 0x58: // Autofocus sensitivity: Not implemented.
                break;
              case 0x5c: // Autofocus frame: Not implemented.
                break;
            }
          break;
        case 0x06:
            switch(command[3]) {
              case 0x01:
              {
                if (len == 6) {
                  // Recall speed.
                  setRecallSpeedVISCA(command[4]);
                } else {
                  // Pan/tilt drive or recall speed
                  while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
                  int16_t panSpeed = command[4];
                  int16_t tiltSpeed = command[5];
                  uint8_t panCommand = command[6];
                  uint8_t tiltCommand = command[7];
                  // 2 is right.  Right is negative.
                  if (panCommand == 2) panSpeed = -panSpeed;
                  else if (panCommand == 3) panSpeed = 0;
                  if (tiltCommand == 2) tiltSpeed = -tiltSpeed;
                  else if (tiltCommand == 3) tiltSpeed = 0;

                  // If there is a move (recall or position set) in progress, ignore any
                  // requests to set the pan or tilt speed to zero, because that means the
                  // operator is not touching the stick.  But if the operator touches the
                  // stick, abort any in-progress move immediately.
                  if (!moveInProgress() || panSpeed != 0 || tiltSpeed != 0) {
                    cancelRecallIfNeeded("Pan/tilt command received");
                    setAxisSpeed(axis_identifier_pan, scaleVISCAPanTiltSpeedToCoreSpeed(panSpeed), false);
                    setAxisSpeed(axis_identifier_tilt, scaleVISCAPanTiltSpeedToCoreSpeed(tiltSpeed), false);
                  }
                }

                while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                return true;
              }
              case 0x02: // Pan/tilt absolute
              {
                 // Do not attempt to use absolute positioning while in calibration mode!
                 if (gCalibrationMode) {
                     fprintf(stderr, "Ignoring absolute pan/tilt while in calibration mode.\n");
                     return false;
                 }

                while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
                int16_t panSpeed = command[4];
                int16_t tiltSpeed = command[5];
                uint16_t rawPanValue = ((command[6] & 0xf) << 12) | ((command[7] & 0xf) << 8) |
                                       ((command[8] & 0xf) << 4) | (command[9] & 0xf);
                uint16_t rawTiltValue = ((command[10] & 0xf) << 12) | ((command[11] & 0xf) << 8) |
                                        ((command[12] & 0xf) << 4) | (command[13] & 0xf);
                int16_t panPosition = (int16_t)rawPanValue;
                int16_t tiltPosition = (int16_t)rawTiltValue;

                cancelRecallIfNeeded("Pan/tilt absolute command received");
                if (!setPanTiltPosition(panPosition, scaleVISCAPanTiltSpeedToCoreSpeed(panSpeed),
                                        tiltPosition, scaleVISCAPanTiltSpeedToCoreSpeed(tiltSpeed),
                                        0)) {
                    return false;
                }

                while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                return true;
              }
              case 0x03: // Pan/tilt relative
              {
                 // Do not attempt to use relative positioning while in calibration mode!
                 if (gCalibrationMode) {
                     fprintf(stderr, "Ignoring relative pan/tilt while in calibration mode.\n");
                     return false;
                 }

                while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
                int16_t panSpeed = command[4];
                int16_t tiltSpeed = command[5];
                uint16_t rawPanPosition = ((command[6] & 0xf) << 12) | ((command[7] & 0xf) << 8) |
                                           ((command[8] & 0xf) << 4) | (command[9] & 0xf);
                uint16_t rawTiltPosition = ((command[10] & 0xf) << 12) | ((command[11] & 0xf) << 8) |
                                            ((command[12] & 0xf) << 4) | (command[13] & 0xf);
                int16_t relativePanPosition = (int16_t)rawPanPosition;
                int16_t relativeTiltPosition = (int16_t)rawTiltPosition;

                int64_t panPosition, tiltPosition;
                if (GET_PAN_TILT_POSITION(&panPosition, &tiltPosition)) {
                    panPosition += relativePanPosition;
                    tiltPosition += relativeTiltPosition;

                    cancelRecallIfNeeded("Pan/tilt relative command received");
                    if (!setPanTiltPosition(panPosition, scaleVISCAPanTiltSpeedToCoreSpeed(panSpeed),
                                            tiltPosition, scaleVISCAPanTiltSpeedToCoreSpeed(tiltSpeed),
                                            0)) {
                        return false;
                    }
                } else {
                    return false;
                }

                while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                return true;
              }
              case 0x07: // Pan/tilt limit set: Not implemented.
                break;
              case 0x35: // Select resolution: Not implemented.
                break;
              case 0x37: // HDMI output range (???)
                break;
            }
          break;

        case 0x7E: // Tally
          if (command[3] == 0x01 && command[4] == 0x0A) {
            if (command[5] == 0x00 && command[7] == 0xFF) {
              // 0x81 01 7E 01 0A 00 0p FF : Tally: p=2: on p=3: off.
              //                             OR p=0: off, p=1: green, p=2: red, p=4: blue.
              switch(command[6]) {
                case 0:
                case 3:
                  if (!setTallyOff()) {
                    return false;
                  }
                  break;
                case 2:
                  if (!setTallyRed()) {
                    return false;
                  }
                  break;
                case 1:
                case 4:
                  if (!setTallyGreen()) {
                    return false;
                  }
                  break;
                default:
                  return false;
              }
              while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
              return true;
            } else if (command[5] == 0x00 && command[7] == 0xFF) {
              // 0x81 01 7E 01 0A 01 0p FF : Tally: 0=off 4=low 5=high red 6=high green 7=disable power light.
              switch(command[6]) {
                case 0:
                  if (!setTallyOff()) {
                    return false;
                  }
                  break;
                case 4:
                case 5:
                  if (!setTallyRed()) {
                    return false;
                  }
                  break;
                case 6:
                  if (!setTallyGreen()) {
                    return false;
                  }
                  break;
                default:
                  return false;
              }
              while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
              return true;
            }
          }
        default:
          break;
      }
    case 0x0A: // Unimplemented
      // 0x81 0a 01 03 10 ff : AF calibration.

      // PTZOptics tally protocol
      // 0x81 0a 02 02 0p ff : Tally: p=1: flashing; p=2: solid; p=3: normal.

      break;
    case 0x0B: // Unimplemented
      // 0x81 0b 01 xx ff : Tally: 01 = high; 02 = medium; 03 = low; 04 = off.
      break;
    case 0x2A: // Unimplemented
      // 0x81 2a 02 a0 04 0p ff: p=2: USB/UAC audio on; p=3: off.
      break;
    default:
      break;
  }
  return false;
}


#pragma mark - Preset management

char *presetFilename(int presetNumber) {
    static char *buf = NULL;
    if (buf != NULL) {
        free(buf);
    }
    asprintf(&buf, "preset_%d", presetNumber);
    return buf;
}

bool savePreset(int presetNumber) {
    preset_t preset;
    bool retval = GET_PAN_TILT_POSITION(&preset.panPosition, &preset.tiltPosition);
    preset.zoomPosition = GET_ZOOM_POSITION();

    if (retval) {
        FILE *fp = fopen(presetFilename(presetNumber), "w");
        fwrite((void *)&preset, sizeof(preset), 1, fp);
        fclose(fp);
        fprintf(stderr, "Saving preset %d (pan=%" PRId64 ", tilt=%" PRId64
                        ", zoom=%" PRId64 "\n",
                presetNumber,
                preset.panPosition,
                preset.tiltPosition,
                preset.zoomPosition);
    } else {
        fprintf(stderr, "Failed to save preset %d\n", presetNumber);
    }

    return retval;
}

int getVISCAZoomSpeedFromTallyState(void) {
    int tallyState = GET_TALLY_STATE();
    bool onProgram = (tallyState == kTallyStateRed);
    return onProgram ? 2 : 8;
}

bool recallPreset(int presetNumber) {
    bool localDebug = false;

    // Do not attempt to recall positions in calibration mode!
    if (gCalibrationMode) {
        fprintf(stderr, "Ignoring recall while in calibration mode.\n");
        return false;
    }

    preset_t preset;
    bzero(&preset, sizeof(preset));  // Zero the structure in case it grows.

    FILE *fp = fopen(presetFilename(presetNumber), "r");
    if (!fp) {
        fprintf(stderr, "Failed to load preset %d (no data)\n", presetNumber);
        return false;
    }
    fread((void *)&preset, sizeof(preset), 1, fp);
    fclose(fp);

    int tallyState = GET_TALLY_STATE();
    bool onProgram = (tallyState == kTallyStateRed);

    // These speeds are used if there is no speed data for any axis.
    int panSpeed = onProgram ? 5 : 24;
    int tiltSpeed = onProgram ? 5 : 24;
    int zoomSpeed = getVISCAZoomSpeedFromTallyState();

    int64_t currentPanPosition = getAxisPosition(axis_identifier_pan);
    int64_t currentTiltPosition = getAxisPosition(axis_identifier_tilt);
    int64_t currentZoomPosition = getAxisPosition(axis_identifier_zoom);

    // To avoid breaking pan and tilt for devices that don't support zoom automation or vice versa,
    // set flags only for axes that are actually moving.
    int flags = ((preset.panPosition != currentPanPosition) ? kFlagMovePan : 0) |
                ((preset.tiltPosition != currentTiltPosition) ? kFlagMoveTilt : 0) |
                ((preset.zoomPosition != currentZoomPosition) ? kFlagMoveZoom : 0);

    if (localDebug) {
      fprintf(stderr, "flags: %d\n", flags);
    }

    double duration = durationForMove(flags, preset.panPosition, preset.tiltPosition, preset.zoomPosition);

    if (duration == 0 && localDebug) {
        fprintf(stderr, "WARNING: durationForMove returned 0\n");
    }

    cancelRecallIfNeeded("recallPreset");
    bool retval = setPanTiltPosition(preset.panPosition, scaleVISCAPanTiltSpeedToCoreSpeed(panSpeed),
                                     preset.tiltPosition, scaleVISCAPanTiltSpeedToCoreSpeed(tiltSpeed),
                                     duration);
    bool retval2 = setZoomPosition(preset.zoomPosition, scaleVISCAZoomSpeedToCoreSpeed(zoomSpeed),
                                   duration);

    if (retval && retval2) {
        fprintf(stderr, "Loaded preset %d\n", presetNumber);
    } else {
        fprintf(stderr, "Failed to load preset %d\n", presetNumber);
        if (!retval) {
            fprintf(stderr, "    Failed to set pan and tilt position.\n");
        }
        if (!retval2) {
            fprintf(stderr, "    Failed to set zoom position.\n");
        }
    }

    return retval && retval2;
}

void setRecallSpeedVISCA(int value) {
  gRecallSpeedSet = true;
  gVISCARecallSpeed = value;
}

/**
 * Returns the duration that a recall should take based on either the current tally state
 * or the speed set via VISCA.
 */
double currentRecallTime(void) {

return 10.0;

  // VISCA (or at least PTZOptics) allows a range of 1 to 24.  This maps those into speeds.
  // These speeds are just sort of arbitrary mappings.
  int recallSpeed = gRecallSpeedSet ? gVISCARecallSpeed : getVISCAZoomSpeedFromTallyState();
  fprintf(stderr, "Recall speed set: %s\n", gRecallSpeedSet ? "YES" : "NO");
  if (gRecallSpeedSet) fprintf(stderr, "Requested recall speed: %d\n", gVISCARecallSpeed);
  fprintf(stderr, "Tally-based speed: %d\n", getVISCAZoomSpeedFromTallyState());

  // Slowest speed is 1, which maps to 30 seconds.
  // Fastest speed is 24, which maps to 2 seconds.
  //
  // From there, we compute an equation to find the other values.
  //
  // k - 24n = 2
  // k - 1n = 30
  // ---------------
  // 24k - 24n = 720
  // -k + 24n = -2
  // 23k     = 718
  // k = 718/23
  // ---------------
  // k - n = 30
  // 718/23 - n = 30
  // 718/23 = 30 + n
  // n = 718/23 - 30
  // n = 28/23  (about 1.2173)

  double multiplier = 28.0/23.0;
  double computed_max = 718.0/23.0;
  double duration = computed_max - (multiplier * recallSpeed);
  return (duration > 30) ? 30 :
         (duration < 2) ? 2 :
         duration;
}


#pragma mark - Calibration

// Computes a map between motor speed and encoder positions per second for each axis.
void do_calibration(void) {
  int localDebug = 0;
  int64_t lastPosition[NUM_AXES];
  int64_t maxPosition[NUM_AXES];
  int64_t minPosition[NUM_AXES];
  bool lastMoveWasPositive[NUM_AXES];  // If true, positive values moved right/down.
  bool lastMoveWasPositiveAtEncoder[NUM_AXES];  // If true, last move increased encoder position.
  bool axisHasMoved[NUM_AXES];

  bzero(&axisHasMoved, sizeof(axisHasMoved));
  bzero(&lastMoveWasPositive, sizeof(lastMoveWasPositive));
  bzero(&lastMoveWasPositiveAtEncoder, sizeof(lastMoveWasPositiveAtEncoder));

  if (!gCalibrationModeQuick) {
    if (localDebug) {
      // This technically happened a bit earlier (before module initialization), but log it
      // onscreen now so that the user will know.
      fprintf(stderr, "Resetting metrics.\n");
    }

    if (localDebug) {
      fprintf(stderr, "Waiting for the system to stabilize.\n");
    }

    // Wait two seconds to ensure everything is up and running.
    usleep(2000000);

    for (axis_identifier_t axis = axis_identifier_pan; axis <= axis_identifier_tilt; axis++) {
      int64_t value = getAxisPosition(axis);
      lastPosition[axis] = value;
      maxPosition[axis] = value;
      minPosition[axis] = value;
    }

    if (localDebug) {
      fprintf(stderr, "Pan maximally left, then right, then tilt maximally up, then down\n");
    }

    // After both pan and tilt axes have moved AND the gimbal has been idle for at
    // least 10 seconds, stop calibrating.
    double lastMoveTime = timeStamp();
    while (!axisHasMoved[axis_identifier_pan] || !axisHasMoved[axis_identifier_tilt] ||
            (timeStamp() - lastMoveTime) < 10) {
      for (axis_identifier_t axis = 0; axis <= axis_identifier_tilt; axis++) {
        if (localDebug > 1) {
          fprintf(stderr, "Processing axis %d\n", axis);
        }

        // See if the position has moved (by enough to matter).
        int64_t value = getAxisPosition(axis);
        bool axisMoved = false;
        if (llabs(lastPosition[axis] - value) > 200) {
          lastMoveTime = timeStamp();
          axisHasMoved[axis] = true;
          axisMoved = true;

          if (value > lastPosition[axis]) {
            lastMoveWasPositiveAtEncoder[axis] = true;
            if (localDebug) {
              fprintf(stderr, "Axis %d moved positively at encoder\n", axis);
            }
          } else {
            lastMoveWasPositiveAtEncoder[axis] = false;
            if (localDebug) {
              fprintf(stderr, "Axis %d moved negatively at encoder\n", axis);
            }
          }
          lastPosition[axis] = value;
        }
        if (value > maxPosition[axis]) {
          maxPosition[axis] = value;
          if (localDebug) {
            fprintf(stderr, "Axis %d new max: %" PRId64 "\n", axis, maxPosition[axis]);
          }
        }
        if (value < minPosition[axis]) {
          minPosition[axis] = value;
          if (localDebug) {
            fprintf(stderr, "Axis %d new min: %" PRId64 "\n", axis, minPosition[axis]);
          }
        }

        // Ignore tiny bits of motion to avoid the risk of self-centering
        // joysticks going slightly too far.
        if (gAxisLastMoveSpeed[axis] > 100 && axisMoved) {
          if (localDebug) {
            fprintf(stderr, "Axis %d moved positively at motor\n", axis);
          }
          lastMoveWasPositive[axis] = true;
        } else if (gAxisLastMoveSpeed[axis] < -100 && axisMoved) {
          lastMoveWasPositive[axis] = false;
          if (localDebug) {
            fprintf(stderr, "Axis %d moved negatively at motor\n", axis);
          }
        }
        usleep(10000);  // Run 100 times per second.
      }
      if (localDebug > 1) {
        fprintf(stderr, "Loop check: panMoved: %s tiltMoved: %s time since last move: %lf\n",
                axisHasMoved[axis_identifier_pan] ? "YES" : "NO",
                axisHasMoved[axis_identifier_tilt] ? "YES" : "NO",
                (timeStamp() - lastMoveTime));
      }
    }

    if (localDebug) {
      fprintf(stderr, "Out of loop.  Writing configuration.\n");
    }

    // Negative motion values should move down and to the right.  If the last move (which
    // should have been to the right or down) was a positive value, then that axis is
    // backwards, and motor speeds should be reversed.
    setConfigKeyBool(kPanMotorReversedKey, lastMoveWasPositive[axis_identifier_pan]);
    setConfigKeyBool(kTiltMotorReversedKey, lastMoveWasPositive[axis_identifier_tilt]);

    // Similarly, if the last move (which should have resulted in a negative change to the
    // encoder position) increased the encoder position, then the encoder for that axis
    // is backwards.
    setConfigKeyBool(kPanEncoderReversedKey, lastMoveWasPositiveAtEncoder[axis_identifier_pan]);
    setConfigKeyBool(kTiltEncoderReversedKey, lastMoveWasPositiveAtEncoder[axis_identifier_tilt]);

    // If the last move (right/down) resulted in encoder values increasing, then:
    //
    // Right is maximum encoder value if last move was increasing, else minimum.
    // Down is maximum encoder value if last move was increasing, else minimum.
    // left is minimum encoder value if last move was increasing, else maximum.
    // Up is minimum encoder value if last move was increasing, else maximum.

    setConfigKeyInteger(kPanLimitLeftKey, lastMoveWasPositiveAtEncoder[axis_identifier_pan] ?
        minPosition[axis_identifier_pan] : maxPosition[axis_identifier_pan]);
    setConfigKeyInteger(kPanLimitRightKey, lastMoveWasPositiveAtEncoder[axis_identifier_pan] ?
        maxPosition[axis_identifier_pan] : minPosition[axis_identifier_pan]);
    setConfigKeyInteger(kTiltLimitTopKey, lastMoveWasPositiveAtEncoder[axis_identifier_tilt] ?
        minPosition[axis_identifier_tilt] : maxPosition[axis_identifier_tilt]);
    setConfigKeyInteger(kTiltLimitBottomKey, lastMoveWasPositiveAtEncoder[axis_identifier_tilt] ?
        maxPosition[axis_identifier_tilt] : minPosition[axis_identifier_tilt]);
  } else {
    fprintf(stderr, "Quick recalibration.  Using defaults.\n");
  }

  fprintf(stderr, "Pan limit left: %" PRId64 "\n", leftPanLimit());
  fprintf(stderr, "Pan limit right: %" PRId64 "\n", rightPanLimit());
  fprintf(stderr, "Pan motor reversed: %s\n", panMotorReversed() ? "YES" : "NO");
  fprintf(stderr, "Pan encoder reversed: %s\n\n", panEncoderReversed() ? "YES" : "NO");

  fprintf(stderr, "Tilt limit up: %" PRId64 "\n", topTiltLimit());
  fprintf(stderr, "Tilt limit down: %" PRId64 "\n", bottomTiltLimit());
  fprintf(stderr, "Tilt motor reversed: %s\n", tiltMotorReversed() ? "YES" : "NO");
  fprintf(stderr, "Tilt encoder reversed: %s\n\n", tiltEncoderReversed() ? "YES" : "NO");

  gCalibrationModeVISCADisabled = true;

  if (!gCalibrationModeZoomOnly) {
    if (localDebug) {
      fprintf(stderr, "Calibrating motor module.\n");
    }

    motorModuleCalibrate();
  }

  if (localDebug) {
    fprintf(stderr, "Calibrating panasonic module.\n");
  }

  panaModuleCalibrate();

  if (localDebug) {
    fprintf(stderr, "Out of loop.  Writing configuration.\n");
  }
  gCalibrationModeVISCADisabled = false;
  gCalibrationMode = false;

  if (!motorModuleReload()) {
    fprintf(stderr, "Motor module reload failed.  Bailing.\n");
    exit(1);
  }

  if (!panaModuleReload()) {
    fprintf(stderr, "Panasonic module reload failed.  Bailing.\n");
    exit(1);
  }
}

double timeStamp(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}

void waitForAxisMove(axis_identifier_t axis) {
  while (1) {
    if (!gAxisMoveInProgress[axis]) {
      break;
    }
    usleep(100000);  // Wake up 10x per second or so.
  }
}

bool pastEnd(int64_t currentPosition, int64_t startPosition, int64_t endPosition, int direction) {
  if (direction == 1) {
    if (startPosition > endPosition) {
      return currentPosition >= startPosition;
    }
    return currentPosition <= startPosition;
  } else {
    if (startPosition > endPosition) {
      return currentPosition <= endPosition;
    }
    return currentPosition >= endPosition;
  }
}

// Returns number of seconds before last valid sample (with ~10,000 usec precision).
double spinAxis(axis_identifier_t axis, int microseconds, int64_t startPosition, int64_t endPosition,
              int direction) {
  bool localDebug = false;

  if (localDebug) {
    fprintf(stderr, "Spinning axis %d for %d microseconds.\n", axis, microseconds);
  }

  double startTime = timeStamp();
  double interval = (double)microseconds / USEC_PER_SEC;
  double endTime = startTime;
  while (true) {
    int64_t currentPosition = getAxisPosition(axis);
    if (pastEnd(currentPosition, startPosition, endPosition, direction)) {
      if (localDebug) {
        fprintf(stderr, "Stopped spinning early (out of bounds)\n");
      }
      return (endTime - startTime);
    }

    endTime = timeStamp();
    if (endTime >= (startTime + interval)) {
      break;
    }
    usleep(10000);  // Wake up 100x per second or so.
  }
  if (localDebug) {
    fprintf(stderr, "Done spinning\n");
  }

  return (endTime - startTime);
}

double calibrationValueForMoveAlongAxis(axis_identifier_t axis,
    int64_t startPosition, int64_t endPosition, int speed, float dutyCycle,
    bool pollingIsSlow) {
  bool localDebug = false;
  int attempts = 0;
  int64_t motionStartPosition = 0;
  int64_t motionEndPosition = 1;

  // If the motor was already moving in the right direction, don't wait as long.
  static bool inMotion = false;

  // If we don't get values because a half second moves too far, set to true.
  bool movedTooFast = false;

  if (localDebug) {
    fprintf(stderr, "Obtaining calibration value for axis %d speed %d (start=%" PRId64
                    ", end=%" PRId64 "\n", axis, speed, startPosition, endPosition);
  }

  static int direction = -1;

  double actualDuration = 0;
  while (attempts++ < 5) {
    if (localDebug) {
      fprintf(stderr, "Setting axis %d to speed %d direction %d\n", axis, speed, direction);
    }
    // Set the axis speed using the "raw" function so that there is no scaling involved.  This
    // avoids any precision loss caused by converting from motor speeds to core speeds and back
    // without having to run the motor at all 1,000 core speeds.
    setAxisSpeedRaw(axis, speed * direction, false);

    // Run the motors for a while before computing the speed.
    float dutyCycleMultiplier = (dutyCycle < .25) ? 2 : (dutyCycle < .50) ? 1.5 : 1;
    if (pollingIsSlow) dutyCycleMultiplier *= 5;
    int delay = (inMotion ? 0 : movedTooFast ? 1000000 : 2000000) * dutyCycleMultiplier;

    if (spinAxis(axis, delay, startPosition, endPosition, direction) >=
        (delay * 1.0 / USEC_PER_SEC)) {
      motionStartPosition = getAxisPosition(axis);

      // Try to sample data for 2 seconds, and throw it away if we can't get at least 1.5
      // seconds of data.  Lower the threshold if we fail twice in a row, because that
      // means it takes less than 2 seconds to move the full distance.  (This shouldn't
      // ever occur in practice.)
      const int duration = movedTooFast ? 1000000 : 2000000;
      const double minValidInterval = movedTooFast ? 1 : 1.5;
      actualDuration = spinAxis(axis, duration, startPosition, endPosition, direction);
      if (actualDuration >= minValidInterval) {
        if (localDebug) {
          fprintf(stderr, "Got speed data.\n");
        }
        break;
      }
    } else if (attempts > 2) {
      if (localDebug) {
        fprintf(stderr, "Moved too fast.\n");
      }
      movedTooFast = true;
    }
    if (localDebug || 1) {
      fprintf(stderr, "Reached end position while computing speed %d.  Reversing direction.\n", speed);
      setAxisSpeedRaw(axis, 0, false);
      usleep(500000);
    }
    direction = -direction;

    inMotion = false;
    attempts++;
  }
  motionEndPosition = getAxisPosition(axis);

  int64_t distance = llabs(motionEndPosition - motionStartPosition);

  inMotion = true;
  double distancePerSecond = ((double)distance / actualDuration);
  if (localDebug) {
    fprintf(stderr, "Returning distance per second %lf\n", distancePerSecond);
  }
  return distancePerSecond;
}

int64_t *calibrationDataForMoveAlongAxis(axis_identifier_t axis,
                                     int64_t startPosition,
                                     int64_t endPosition,
                                     int32_t minSpeed,
                                     int32_t maxSpeed,
                                     bool pollingIsSlow) {
  bool localDebug = false;
  if (localDebug) {
    fprintf(stderr, "Gathering calibration data for axis %d\n", axis);
  }

  int64_t *data = (int64_t *)malloc(sizeof(int64_t) * (maxSpeed - minSpeed + 1));
  setAxisPositionIncrementally(axis, startPosition, SCALE_CORE, 0);  // Move as quickly as possible.
  waitForAxisMove(axis);

  if (localDebug) {
    fprintf(stderr, "Finished initial move.\n");
  }

  for (int32_t speed = minSpeed; speed <= maxSpeed; speed++) {
    float dutyCycle = (speed - minSpeed) / (float)(maxSpeed - minSpeed);

    bool done = false;

#define NUM_SAMPLES 10
#define MIN_SAMPLES 4

    double positionsPerSecond[NUM_SAMPLES];
    double positionsPerSecondAverage = 0;
    int failures = 0;
    while (!done) {
      // No need to invert the drive direction.  The motor driver should already be
      // handling that.
      int64_t min = 0, max = 0;
      int64_t sameValue = -1;
      for (int i = 0 ; i < NUM_SAMPLES; i++) {
        double value =
            calibrationValueForMoveAlongAxis(axis, startPosition, endPosition, speed, dutyCycle,
                                             pollingIsSlow);
        positionsPerSecond[i] = value;
        if (i == 0) {
          sameValue = value;
          min = value;
          max = value;
        } else if (value > max) {
          max = value;
        } else if (value < min) {
          min = value;
        }
        fprintf(stderr, "Positions per second at speed %d [%d]: %lf\n",
                speed, i, positionsPerSecond[i]);

        // If we get MIN_SAMPLES identical values immediately (realistically because this uses
        // a floating-point value, this always means that the motor isn't moving), don't bother
        // getting any more values.
        if (value == sameValue && i == (MIN_SAMPLES - 1)) {
          // We got MIN_SAMPLES with identical values.  Bail early.
          done = true;
          break;
        } else if (value != sameValue) {
          sameValue = -1;
        }
      }
      if (!done) {
        if (min == max) {
          positionsPerSecondAverage = min;
          done = true;
        } else {
          int64_t alltotal = 0;
          int64_t total = 0;
          int count = 0;
          // int64_t tempmin = min, tempmax = max;
          int64_t newmin = -1, newmax = -1;
          for (int i = 0 ; i < NUM_SAMPLES; i++) {
            int64_t value = positionsPerSecond[i];
            alltotal += value;
          }
          double mean = (double)alltotal / NUM_SAMPLES;
          double sumOfSquares = 0;
          for (int i = 0 ; i < NUM_SAMPLES; i++) {
            int64_t value = positionsPerSecond[i];
            double deviation = value - mean;
            sumOfSquares += (deviation * deviation);
          }
          double standardDeviation = sqrt(sumOfSquares / NUM_SAMPLES);
          for (int i = 0 ; i < NUM_SAMPLES; i++) {
            int64_t value = positionsPerSecond[i];

            if (fabs(value - mean) > standardDeviation) {
              fprintf(stderr, "Discarding outlier %" PRId64 ".\n", value);
              continue;
            }
            total += value;
            if (newmin == -1 || value < newmin) {
              newmin = value;
            }
            if (newmax == -1 || value > newmax) {
              newmax = value;
            }
            count++;
          }
          positionsPerSecondAverage = (double)total / count;

          double error = (double)newmax - newmin;
          double errorPercent = error / newmax;
          if (error <= 2 || errorPercent < .1) {
            done = true;
          } else if (failures >= 3) {
            done = true;
            fprintf(stderr, "Total error is still too large, but too many retries, so giving up.\n");
          } else {
            fprintf(stderr, "Total error %lf > 2 and error percent %lf >= .1.  Trying again.\n",
                    error, errorPercent);
            failures++;
          }
        }
      }
    }

    int index = speed - minSpeed;
    data[index] = round(positionsPerSecondAverage);
    fprintf(stderr, "Positions per second at speed %d (average): %lf (%lld)\n",
            index, positionsPerSecondAverage, data[index]);

    // The motor may stall at low voltages, but once it gets moving, it should get faster
    // for each increase in voltage.  If not, something went wrong, and our results are invalid.
    // In some cases (e.g. for zoom motors hidden behind software), two speed values might result
    // in identical speeds, so it's potentially okay for it to not speed up, but it should never
    // slow down.
    if (speed >= 1 && data[index] < data[index - 1] && data[index] > 0) {
      if (pollingIsSlow) {
        fprintf(stderr, "Motor slowed down.  Assuming speed is unchanged.\n");
        data[index] = data[index - 1];
      } else {
        fprintf(stderr, "Motor slowed down.  Recomputing previous position and current position.\n");
        speed -= 2;
      }
    }
  }
  setAxisSpeedRaw(axis, 0, false);
  if (localDebug) {
    fprintf(stderr, "Done collecting data for axis %d\n", axis);
  }

  return data;
}

const char *nameForAxis(axis_identifier_t axis) {
  switch (axis) {
    case axis_identifier_pan:
      return "pan";
    case axis_identifier_tilt:
      return "tilt";
      break;
    case axis_identifier_zoom:
      return "zoom";
    default:
      return "unknown";
  }
}

const char *calibrationDataKeyNameForAxis(axis_identifier_t axis) {
  switch (axis) {
    case axis_identifier_pan:
      return "calibration_data_pan";
    case axis_identifier_tilt:
      return "calibration_data_tilt";
      break;
    case axis_identifier_zoom:
      return "calibration_data_zoom";
    default:
      return "calibration_data_unknown";
  }
}

// Public function.  Docs in header.
//
// Reads calibration data from the configuration file.
int64_t *readCalibrationDataForAxis(axis_identifier_t axis,
                                    int *maxSpeed) {
  char *rawCalibrationData = getConfigKey(calibrationDataKeyNameForAxis(axis));
  if (rawCalibrationData == NULL) {
    return NULL;
  }

  int64_t *data = NULL;
  ssize_t size = 0;
  int count = 0;
  char *pos = rawCalibrationData;
  while (*pos != '\0') {
    int64_t value = strtoull(pos, NULL, 10);
    int64_t *newData = realloc(data, size + sizeof(int64_t));
    size += sizeof(int64_t);
    if (newData == NULL) {
      // This should never occur.
      free(data);
      free(rawCalibrationData);
      return NULL;
    }
    data = newData;
    data[count++] = value;

    // Skip to the next value.
    while (*pos && *pos != ' ') {
      pos++;
    }
    while (*pos && *pos == ' ') {
      pos++;
    }
  }

  free(rawCalibrationData);
  if (maxSpeed) {
    // Return the last index.
    *maxSpeed = count - 1;
  }
  return data;
}

// Public function.  Docs in header.
//
// Writes calibration data to the configuration file.
bool writeCalibrationDataForAxis(axis_identifier_t axis, int64_t *calibrationData, int maxSpeed) {
  char *stringData = NULL;
  asprintf(&stringData, "%s", "");
  for (int i = 0; i <= maxSpeed; i++) {
    char *previousStringData = stringData;
    asprintf(&stringData, "%s %" PRId64, previousStringData, calibrationData[i]);
    free(previousStringData);
  }
  bool retval = setConfigKey(calibrationDataKeyNameForAxis(axis), stringData + 1);
  free(stringData);
  return retval;
}


#pragma mark - Pan and tilt direction information.

/** Purges all calibration data from the configuration file. */
bool resetCalibration(void) {
  bool retval = true;
  retval = removeConfigKey(kPanMotorReversedKey) && retval;
  retval = removeConfigKey(kTiltMotorReversedKey) && retval;
  retval = removeConfigKey(kZoomMotorReversedKey) && retval;
  retval = removeConfigKey(kPanEncoderReversedKey) && retval;
  retval = removeConfigKey(kTiltEncoderReversedKey) && retval;
  retval = removeConfigKey(kZoomEncoderReversedKey) && retval;
  retval = removeConfigKey(kPanLimitLeftKey) && retval;
  retval = removeConfigKey(kPanLimitRightKey) && retval;
  retval = removeConfigKey(kTiltLimitTopKey) && retval;
  retval = removeConfigKey(kTiltLimitBottomKey) && retval;
  return retval;
}

bool panMotorReversed(void) {
  return getConfigKeyBool(kPanMotorReversedKey);
}

bool tiltMotorReversed(void) {
  return getConfigKeyBool(kTiltMotorReversedKey);
}

bool zoomMotorReversed(void) {
  return getConfigKeyBool(kZoomMotorReversedKey);
}

bool panEncoderReversed(void) {
  return getConfigKeyBool(kPanEncoderReversedKey);
}

bool tiltEncoderReversed(void) {
  return getConfigKeyBool(kTiltEncoderReversedKey);
}

int64_t leftPanLimit(void) {
  return getConfigKeyInteger(kPanLimitLeftKey);
}

int64_t rightPanLimit(void) {
  return getConfigKeyInteger(kPanLimitRightKey);
}

int64_t topTiltLimit(void) {
  return getConfigKeyInteger(kTiltLimitTopKey);
}

int64_t bottomTiltLimit(void) {
  return getConfigKeyInteger(kTiltLimitBottomKey);
}

int64_t setZoomInLimit(int64_t limit) {
  return setConfigKeyInteger(kZoomInLimitKey, limit);
}

int64_t setZoomOutLimit(int64_t limit) {
  return setConfigKeyInteger(kZoomOutLimitKey, limit);
}

int64_t setZoomEncoderReversed(bool isReversed) {
  return setConfigKeyBool(kZoomEncoderReversedKey, isReversed);
}

int64_t setZoomMotorReversed(bool isReversed) {
  return setConfigKeyBool(kZoomMotorReversedKey, isReversed);
}

int64_t zoomInLimit(void) {
  return getConfigKeyInteger(kZoomInLimitKey);
}

int64_t zoomOutLimit(void) {
  return getConfigKeyInteger(kZoomOutLimitKey);
}

int64_t zoomEncoderReversed(void) {
  return getConfigKeyInteger(kZoomEncoderReversedKey);
}


#pragma mark - Named tally source support (Tricaster, OBS, etc.)

#if TALLY_SOURCE_NAME_REQUIRED
  void setTallySourceName(char *tallySourceName) {
    setConfigKey(kTallySourceName, tallySourceName);
  }
#endif


#pragma mark - Panasonic camera support

#ifdef SET_IP_ADDR
  void setCameraIP(char *cameraIP) {
    setConfigKey(kCameraIPKey, cameraIP);
  }
#endif


#pragma mark - Tricaster support

#if USE_TRICASTER_TALLY_SOURCE
void setTricasterIP(char *TricasterIP) {
    setConfigKey(kTricasterIPKey, TricasterIP);
}
#endif


#pragma mark - OBS support

#if USE_OBS_TALLY_SOURCE
  void setOBSPassword(char *password) {
    setConfigKey(kOBSPasswordKey, password);
  }

  void setOBSWebSocketURL(char *OBSWebSocketURL) {
    setConfigKey(kOBSWebSocketURLKey, OBSWebSocketURL);
  }
#endif


#pragma mark - Tests

void runStartupTests(void) {
  char *bogusValue = getConfigKey("nonexistentKey");
  assert(bogusValue == NULL);

  assert(setConfigKey("key1", "value1"));
  assert(setConfigKey("key2", "value2"));

  char *value1 = getConfigKey("key1");
  assert(!strcmp(value1, "value1"));

  char *value2 = getConfigKey("key2");
  assert(!strcmp(value2, "value2"));

  assert(setConfigKey("key1", "value3"));
  value1 = getConfigKey("key1");
  assert(!strcmp(value1, "value3"));
  assert(!strcmp(value2, "value2"));

  assert(setConfigKey("key2", "value4"));
  value2 = getConfigKey("key2");
  assert(!strcmp(value2, "value4"));

  srand(time(NULL));
  int value = rand();
  assert(setConfigKeyInteger("randomValue", value));
  assert(getConfigKeyInteger("randomValue") == value);

  int64_t fakeData[] = { 0, 10, 20, 30 };
  assert(writeCalibrationDataForAxis(30, fakeData, (sizeof(fakeData) / sizeof(int64_t)) - 1));

  int maxSpeed = 0;
  int64_t *fakeData2 = readCalibrationDataForAxis(30, &maxSpeed);
  assert(maxSpeed == 3);

  for (int i=0; i <= maxSpeed; i++) {
    assert(fakeData[i] == fakeData2[i]);
  }

  int32_t *translatedData = convertSpeedValues(fakeData, 3, 0);
  int32_t expectedResuls[] = { 0, 285, 571, 857 };
  assert(writeCalibrationDataForAxis(30, fakeData, sizeof(fakeData) / sizeof(int64_t)));

  for (int i=0; i <= maxSpeed; i++) {
    assert(translatedData[i] == expectedResuls[i]);
  }

  int64_t source1000Values[] = {
    0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000
  };
  int64_t source100Values[] = {
    0,  10,  20,  30,  40,  50,  60,  70,  80,  90,  100
  };
  int64_t expectedValues[] = {
    0,   1,   1,   1,   1,   2,   2,   2,   3,   3,    3
  };

  // Verify the results for inputs at SCALE_CORE (positive).
  for (int i = 0; i < (sizeof(source1000Values) / sizeof(source1000Values[0])); i++) {
    assert(scaleSpeed(source1000Values[i], SCALE_CORE, maxSpeed, translatedData) == expectedValues[i]);
  }

  // Verify the results for inputs at an arbitrary scale (positive).
  for (int i = 0; i < (sizeof(source100Values) / sizeof(source100Values[0])); i++) {
    assert(scaleSpeed(source100Values[i], 100, maxSpeed, translatedData) == expectedValues[i]);
  }

  // Verify the results for inputs at SCALE_CORE (negative).
  for (int i = 0; i < (sizeof(source1000Values) / sizeof(source1000Values[0])); i++) {
    assert(scaleSpeed(-source1000Values[i], SCALE_CORE, maxSpeed, translatedData) == -expectedValues[i]);
  }

  // Verify the results for inputs at an arbitrary scale (negative).
  for (int i = 0; i < (sizeof(source100Values) / sizeof(source100Values[0])); i++) {
    assert(scaleSpeed(-source100Values[i], 100, maxSpeed, translatedData) == -expectedValues[i]);
  }
}

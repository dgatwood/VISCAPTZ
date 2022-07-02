// #include <csignal>
// #include <cstddef>
// #include <cstdio>
// #include <atomic>
// #include <chrono>
// #include <string>
// #include <thread>

#include "configurator.h"
#include "constants.h"
#include "fakeptz.h"
#include "main.h"
#include "motorptz.h"
#include "panasonicptz.h"

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

static int kAxisStallThreshold = 100;
const char *kPanMotorReversedKey = "pan_axis_motor_reversed";
const char *kTiltMotorReversedKey = "tilt_axis_motor_reversed";
const char *kPanEncoderReversedKey = "pan_axis_encoder_reversed";
const char *kTiltEncoderReversedKey = "tilt_axis_encoder_reversed";
const char *kPanLimitLeftKey = "pan_limit_left";
const char *kPanLimitRightKey = "pan_limit_right";
const char *kTiltLimitTopKey = "tilt_limit_up";
const char *kTiltLimitBottomKey = "tilt_limit_down";


#pragma mark - Structures and prototypes

typedef struct {
  uint8_t cmd[2];
  uint16_t len;
  uint32_t sequence_number;
  uint8_t data[65519];  // Maximum theoretical IPv6 UDP packet size minus above.
} visca_cmd_t;

typedef struct {
  uint8_t cmd[2];
  uint16_t len;  // Big endian!
  uint32_t sequence_number;
  uint8_t data[65527];  // Maximum theoretical IPv6 UDP packet size.
} visca_response_t;

#define NUM_AXES (axis_identifier_zoom + 1)

static bool gAxisMoveInProgress[NUM_AXES];
static int64_t gAxisMoveStartPosition[NUM_AXES];
static int64_t gAxisMoveTargetPosition[NUM_AXES];
static int64_t gAxisMoveMaxSpeed[NUM_AXES];
static int64_t gAxisLastMoveSpeed[NUM_AXES];
static int64_t gAxisPreviousPosition[NUM_AXES];
static int gAxisStalls[NUM_AXES];

int debugPanAndTilt = 0; // kDebugModePan;// kDebugModeZoom;  // Bitmap from debugMode.

void run_startup_tests(void);

pthread_t network_thread;
void *runPTZThread(void *argIgnored);
void *runNetworkThread(void *argIgnored);
bool handleVISCAPacket(visca_cmd_t command, int sock, struct sockaddr *client, socklen_t structLength);
int getVISCAZoomSpeedFromTallyState(void);

bool absolutePositioningSupportedForAxis(axis_identifier_t axis);
void handleRecallUpdates(void);
int64_t getAxisPosition(axis_identifier_t axis);
bool setAxisPositionIncrementally(axis_identifier_t axis, int64_t position, int64_t maxSpeed);
bool setAxisSpeed(axis_identifier_t axis, int64_t position, bool debug);
bool setAxisSpeedRaw(axis_identifier_t axis, int64_t speed, bool debug);
void cancelRecallIfNeeded(char *context);

visca_response_t *failedVISCAResponse(void);
visca_response_t *enqueuedVISCAResponse(void);
visca_response_t *completedVISCAResponse(void);

void *runMotorControlThread(void *argIgnored);

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

pthread_t motor_control_thread;

bool gCalibrationMode = false;
bool gCalibrationModeQuick = false;
bool gCalibrationModeVISCADisabled = false;

bool resetCalibration(void);
void do_calibration(void);

#pragma mark - Main

int main(int argc, char *argv[]) {

  run_startup_tests();

  if (argc >= 2) {
    if (!strcmp(argv[1], "--calibrate")) {
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
    }
  }
#if USE_CANBUS
  if (argc >= 4) {
    if (!strcmp(argv[1], "--reassign")) {
      int oldCANBusID = atoi(argv[2]);
      int newCANBusID = atoi(argv[3]);
      reassign_encoder_device_id(oldCANBusID, newCANBusID);
    }
  }
#endif

  pthread_create(&network_thread, NULL, runNetworkThread, NULL);

  SET_IP_ADDR("127.0.0.1");

  if (!motorModuleInit()) {
    fprintf(stderr, "Motor module init failed.  Bailing.\n");
    exit(1);
  }
  if (!panaModuleInit()) {
    fprintf(stderr, "Panasonic module init failed.  Bailing.\n");
    exit(1);
  }

  pthread_create(&motor_control_thread, NULL, runMotorControlThread, NULL);

  fprintf(stderr, "Created threads.\n");

  if (gCalibrationMode) {
    fprintf(stderr, "Starting calibration.\n");
    do_calibration();
  }

  fprintf(stderr, "Ready for VISCA commands.\n");

  // Spin this thread forever for now.
  while (1) {
    usleep(1000000);
  }
}

#pragma mark - Testing implementation (no-ops)

bool debugSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed, bool isRaw) {
  fprintf(stderr, "setPanTiltSpeed: pan speed: %" PRId64 "\n"
                  "tilt speed: %" PRId64 " raw: %s\n", panSpeed, tiltSpeed, isRaw ? "YES" : "NO");
  return false;
}

bool debugSetZoomSpeed(int64_t speed) {
  fprintf(stderr, "setZoomSpeed: %" PRId64 "\n", speed);
  return false;
}

bool debugGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition) {
  fprintf(stderr, "getPanPosition\n");
  if (panPosition != NULL) {
    *panPosition = 0.0;
  }
  if (tiltPosition != NULL) {
    *tiltPosition = 0.0;
  }
  return true;
}

int64_t debugGetZoomPosition(void) {
  // Future versions will return a position (arbitrary scale).
  // Return 0 if the value is "close enough" that motion is impossible.
  fprintf(stderr, "getZoomPosition\n");
  return 0.0;
}

int debugGetTallyState(void) {
  FILE *fp = fopen("/var/tmp/tallyState", "r");
  if (!fp) return 0;
  char data[100];
  fgets(data, 99, fp);
  fclose(fp);
  // fprintf(stderr, "Tally state: %d\n", atoi(data));
  return atoi(data);
}

#pragma mark - Generic move routines

int actionProgress(int64_t startPosition, int64_t curPosition, int64_t endPosition,
                   int64_t previousPosition, int *stalls) {
  bool localDebug = false;
  int64_t progress = llabs(curPosition - startPosition);
  int64_t total = llabs(endPosition - startPosition);

  if (total == 0) {
    fprintf(stderr, "End position (%" PRId64 ") = start position (%" PRId64 ").  Doing nothing.\n",
            startPosition, endPosition);
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

  // If we're moving too quickly, hit the brakes.
  int64_t increment = llabs(curPosition - previousPosition);
  if ((increment + progress) > total) {
    tenth_percent = MAX(975, tenth_percent);
  } else if ((increment + (2 * progress)) > total) {
    tenth_percent = MAX(950, tenth_percent);
  } else if ((increment + (4 * progress)) > total) {
    tenth_percent = MAX(900, tenth_percent);
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
int computeSpeed(int progress) {
    if (progress < 100 || progress > 900) {
        // For now, we evenly ramp up to full speed at 10% progress and down starting at 90%.
        // This is *NOT* ideal, particularly if we use P2 protocol and get zoom position updates
        // only once per second.
        int distance_to_nearest_endpoint = (progress >= 900) ? (1000 - progress) : progress;

        // With vertical acccuracy of ± .1% at the two endpoints (i.e. 0.001 at the bottom,
        //     0.999 at the top):
        // 1 / (1 + e^-x) would give us a curve of length 14 from -7 to 7.
        // 1 / (1 + e^-(x*7)) would give us a curve of length 14 from -1 to 1.
        // 1 / (1 + e^-((x*7 / 50)) would give us a curve of length 100 from -50 to 50.
        // 1 / (1 + e^-(((x - 50)*7 / 50)) gives us a curve from 0 to 100 (one tenth the
        //                                 progress period).
        // 1 / (1 + e^-(7x/50 - 7)) is simplified.
        // 1 / (1 + e^(7 - (7x/50))) is fully simplified.
        //
        // In the future, this will move to a time-based curve, rather than a distance-based
        // curve.  The explanation below tells how this will happen.
        //
        // The integral of this is -(1/7)((-50 * ln (1+e^(7-(7x /50))))-7x+350).
        // Evaluated from 0 to 100, this gives us 50.0065104747 - 0.00651047466,
        // or exactly an area of 50 under the curve.  So the s-curves average 50%
        // across their duration.
        //
        // This means that the average speed across the entire time range from 0 to 1000
        // is ((max_speed * 800) + (0.5 * max_speed * 200)) / 1000.  We can simplify this
        // to 0.9 * max_speed.
        //
        // So if we know that we want a motion to take 10 seconds (for example), and if
        // that motion is 5000 units of distance, it needs to move 500 units per second,
        // on average.  Since we know that the average speed of the move, including the
        // two curves, will be only 0.9 times the speed that the motor moves during the
        // middle 80% of the move, that means that the peak speed during the middle 80%
        // should be 10/9ths of 500, or about 555.55.
        //
        // With that, we can calculate the exact fraction of maximum speed that the
        // motors should run.
        //
        // However, motors are nonlinear.  They don't move at all at low power, and
        // their speed isn't exactly linear in the duty cycle.  To compute the actual
        // output levels correctly, we need to do a calibration stage.  Here's how that
        // will work:
        //
        // 1.  Run ./viscaptz --calibrate
        // 2.  Quickly figure out which way the pan control moves the camera, and
        //     which way the tilt moves the camera.
        // 3.  Move the camera to the leftmost position that you're comfortable
        //     using, followed by the rightmost position.
        // 4.  Move the camera to the maximum upwards position that you're comfortable
        //     using, followed by the maximum downwards position.
        // 5.  Wait ten seconds without moving the camera.  The software will interpet
        //     the last horizontal move direction to be right, and the last vertical
        //     move direction to be down, and will invert the motion on that axis as
        //     needed to ensure that VISCA left/right/up/down moves go in the right
        //     direction.
        // 6.  The camera will then pan towards the left at progressively increasing
        //     speeds, computing the number of positions per second at each speed,
        //     rewinding to the right when it hits the left edge.
        // 7.  The camera will perform a similiar calibration for the tilt.
        // 8.  The camera will perform a similiar calibration for the zoom.
        //
        // The result will be a table of positions-per-second values for each speed
        // value.  At run time, the app might convert this into a balanced binary tree
        // (if performance necessitates it) to more quickly generate the ideal actual
        // core speed value (from 0 to 1000) to achieve the desired theoretical core
        // speed value.
        double exponent = 7.0 - ((7.0 * distance_to_nearest_endpoint) / 50.0);
        double speedFromProgress = 1 / (1 + pow(M_E, exponent));
        return round(speedFromProgress * 1000.0);
    } else {
        return 1000;
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

      // Compute how far into the motion we are (with a range of 0 to 1,000).
      int direction = (gAxisMoveTargetPosition[axis] > gAxisMoveStartPosition[axis]) ? 1 : -1;
      if (localDebug) {
        fprintf(stderr, "Axis %d direction %d\n", axis, direction);
      }

      // Left/up are positive for the motor.  Right/down are negative.
      //
      // Normal encoder:   Higher values are left.  So a higher value (left of current) means
      //                   positive motor speeds
      // Reversed encoder: Higher values are right.  So a higher value (right of current) means
      //                   negative motor speeds.
      if ((axis == axis_identifier_pan) && panEncoderReversed()) {
        if (localDebug) {
          fprintf(stderr, "Axis %d reversed\n", axis);
        }
        direction = -direction;
        if (localDebug) {
          fprintf(stderr, "Axis %d direction now %d\n", axis, direction);
        }
      } else if ((axis == axis_identifier_tilt) && tiltEncoderReversed()) {
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
      int panProgress = actionProgress(gAxisMoveStartPosition[axis], axisPosition,
                                       gAxisMoveTargetPosition[axis], gAxisPreviousPosition[axis],
                                       &gAxisStalls[axis]);
      gAxisPreviousPosition[axis] = axisPosition;

      if (panProgress == 1000) {
        if (localDebug) {
            fprintf(stderr, "AXIS %d MOTION COMPLETE\n", axis);
        }
        gAxisMoveInProgress[axis] = false;
        setAxisSpeed(axis, 0, false);
      } else {      
        // Scale based on max speed out of 24 (VISCA percentage) and scale to a range of
        // -1000 to 1000 (core speed).
        int speed = MAX(computeSpeed(panProgress), MIN_PAN_TILT_SPEED);
        setAxisSpeed(axis, speed * direction, localDebug);
        if (localDebug) {
            fprintf(stderr, "AXIS %d SPEED NOW %d * %d (%d)\n",
                    axis, speed, direction, speed * direction);
        }
      }
    } else if (localDebug > 1) {
      fprintf(stderr, "NOT UPDATING AXIS %d: NOT IN MOTION\n", axis);
    }
  }
}

double absceil(double value) {
  return (value > 0) ? ceil(value) : floor(value);
}

// 0 should be 0.  All other values should be equiproportional.
int scaleSpeed(int speed, int fromScale, int toScale) {
  return absceil((speed * 1.0 * toScale) / fromScale);
}

int64_t scaleVISCAPanTiltSpeedToCoreSpeed(int speed) {
  return scaleSpeed(speed, PAN_SCALE_VISCA, SCALE_CORE);
}

int64_t scaleVISCAZoomSpeedToCoreSpeed(int speed) {
  // fprintf(stderr, "VISCA speed %d\n", speed);
  return scaleSpeed(speed, ZOOM_SCALE_VISCA, SCALE_CORE);
}

int64_t getAxisPosition(axis_identifier_t axis) {
  switch(axis) {
    case axis_identifier_pan:
    case axis_identifier_tilt:
    {
        int64_t panPosition, tiltPosition;
        if (GET_PAN_TILT_POSITION(&panPosition, &tiltPosition)) {
            return (axis == axis_identifier_pan) ? panPosition : tiltPosition;
        }
    }
    case axis_identifier_zoom:
        return GET_ZOOM_POSITION();
  }
  return 0;
}

bool setZoomPosition(int64_t position, int64_t speed) {
    fprintf(stderr, "@@@ setZoomPosition: %" PRId64 " Speed: %" PRId64 "\n", position, speed);
    return SET_ZOOM_POSITION(position, speed);
}

bool setPanTiltPosition(int64_t panPosition, int64_t panSpeed,
                        int64_t tiltPosition, int64_t tiltSpeed) {
    fprintf(stderr, "@@@ setPanTiltPosition: Pan position %" PRId64 " speed %" PRId64 "\n"
                    "                        Tilt position %" PRId64 " speed %" PRId64 "\n",
                    panPosition, panSpeed, tiltPosition, tiltSpeed);

    if (!absolutePositioningSupportedForAxis(axis_identifier_pan) ||
        !absolutePositioningSupportedForAxis(axis_identifier_tilt)) {
        fprintf(stderr, "Pan/tilt absolute positioning unsupported.");
        return false;
    }

    return SET_PAN_TILT_POSITION(panPosition, panSpeed, tiltPosition, tiltSpeed);
}

bool setAxisSpeedInternal(axis_identifier_t axis, int64_t speed, bool debug, bool isRaw);

bool setAxisSpeed(axis_identifier_t axis, int64_t speed, bool debug) {
  return setAxisSpeedInternal(axis, speed, debug, false);
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

  bool reversed = (axis == axis_identifier_pan) ? panMotorReversed() : tiltMotorReversed();
  if (debug) {
    fprintf(stderr, "Reverse motor direction for axis %d: %s\n", axis, reversed ? "YES" : "NO");
  }

  gAxisLastMoveSpeed[axis] = speed;

  int64_t speedReversedIfNeeded = reversed ? -speed : speed;
  switch(axis) {
    case axis_identifier_pan:
        return SET_PAN_TILT_SPEED(speedReversedIfNeeded, gAxisLastMoveSpeed[axis_identifier_tilt], isRaw);
    case axis_identifier_tilt:
        return SET_PAN_TILT_SPEED(gAxisLastMoveSpeed[axis_identifier_pan], speedReversedIfNeeded, isRaw);
    case axis_identifier_zoom:
        return SET_ZOOM_SPEED(speedReversedIfNeeded, isRaw);
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

#if 0
// I'm pretty sure this is a mistake.
bool calibrationSetAxisPosition(axis_identifier_t axis, int64_t position, int64_t maxSpeed) {
  if (!absolutePositioningSupportedForAxis(axis)) {
    fprintf(stderr, "Absolute positioning not supported.\n");
    return false;
  }
  switch(axis) {
    case axis_identifier_pan:
    {
      int64_t tiltPosition = getAxisPosition(axis_identifier_tilt);
      setPanTiltPosition(position, maxSpeed, tiltPosition, maxSpeed);
      return true;
    }
    case axis_identifier_tilt:
    {
      int64_t panPosition = getAxisPosition(axis_identifier_pan);
      setPanTiltPosition(panPosition, maxSpeed, position, maxSpeed);
      return true;
    }
    case axis_identifier_zoom:
      setZoomPosition(position, maxSpeed);
      return true;
  }
  return false;
}
#endif

bool setAxisPositionIncrementally(axis_identifier_t axis, int64_t position, int64_t maxSpeed) {
  bool localDebug = false;
  if (localDebug) {
    fprintf(stderr, "setAxisPositionIncrementally\n");
  }
  if (!absolutePositioningSupportedForAxis(axis)) {
    fprintf(stderr, "Absolute positioning not supported.\n");
    return false;
  }

  gAxisMoveInProgress[axis] = true;
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

bool sendVISCAResponse(visca_response_t *response, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength);

void *runNetworkThread(void *argIgnored) {
  // Listen on UDP port 52381.
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
    timeout.tv_usec = 10000;  // 10 millisecond update interval.

    if (select(sock + 1, &read_fds, NULL, NULL, &timeout) > 0) {
      int bytes_received = recvfrom(sock, &command, sizeof(visca_cmd_t), 0, 
        (struct sockaddr *) &client, &structLength);

      // fprintf(stderr, "Got packet.\n");
      if (bytes_received < 0) {
        perror("recvfrom");
        // exit(EXIT_FAILURE);
      } else if (structLength > 0) {
        bool success = handleVISCAPacket(command, sock, (struct sockaddr *)&client, structLength);
        if (!success) {
          fprintf(stderr, "VISCA error\n");
          while (!sendVISCAResponse(failedVISCAResponse(), command.sequence_number, sock, (struct sockaddr *)&client, structLength));
        // } else {
          // fprintf(stderr, "Success\n");
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

bool handleVISCAInquiry(uint8_t *command, uint8_t len, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength);
bool handleVISCACommand(uint8_t *command, uint8_t len, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength);

void printbuf(uint8_t *buf, int len) {
  fprintf(stderr, "BUF: ");
  for (int i = 0; i < len; i++) {
    fprintf(stderr, " %02x", buf[i]);
  }
  fprintf(stderr, "\n");
}

bool handleVISCAPacket(visca_cmd_t command, int sock, struct sockaddr *client, socklen_t structLength) {
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

#define SET_RESPONSE(response, array) setResponseArray(response, array, (uint8_t)(sizeof(array) / sizeof(array[0])))
void setResponseArray(visca_response_t *response, uint8_t *array, uint8_t count);

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
  data[2] = (tallyState == 0) ? 2 : 3;
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
  // All VISCA inquiries start with 0x90.
  // fprintf(stderr, "INQUIRY\n");
  if(command[0] != 0x81) return false;

  switch(command[1]) {
    case 0x09:
      switch(command[2]) {
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

bool setTallyOff() {
#ifdef SET_TALLY_STATE
  return SET_TALLY_STATE(kTallyStateOff);
#else
  return false;
#endif
}

bool setTallyRed() {
#ifdef SET_TALLY_STATE
  return SET_TALLY_STATE(kTallyStateRed);
#else
  return false;
#endif
}

bool setTallyGreen() {
#ifdef SET_TALLY_STATE
  return SET_TALLY_STATE(kTallyStateGreen);
#else
  return false;
#endif
}

void setResponseArray(visca_response_t *response, uint8_t *array, uint8_t count) {
  response->cmd[0] = 0x01;
  response->cmd[1] = 0x11;
  response->len = htons(count);
  bcopy(array, response->data, count);
}

// Legal preset values are in the range 0..255.
bool savePreset(int presetNumber);
bool recallPreset(int presetNumber);

bool handleVISCACommand(uint8_t *command, uint8_t len, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength) {

  if (gCalibrationModeVISCADisabled) {
    return false;
  }

  // fprintf(stderr, "COMMAND\n");
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

                int8_t zoomSpeed = 0;
                if (zoomCmd != 0) {  // Don't set a speed if the command is "zoom stop".
                    int8_t zoomRawSpeed = command[4] & 0xf;

                    // VISCA zoom speeds go from 0 to 7, but we need to treat 0 as stopped, so immediately
                    // convert that to be from 1 to 8.
                    zoomSpeed = ((zoomCmd & 0xf0) == 0x20) ? zoomRawSpeed + 1 : - (zoomRawSpeed + 1);
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
                setZoomPosition(position, scaleVISCAZoomSpeedToCoreSpeed(speed));

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
              case 0x01: // Pan/tilt drive
{
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
                                           tiltPosition, scaleVISCAPanTiltSpeedToCoreSpeed(tiltSpeed))) {
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
                                               tiltPosition, scaleVISCAPanTiltSpeedToCoreSpeed(tiltSpeed))) {
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


#pragma mark - Presets

char *presetFilename(int presetNumber) {
    static char *buf = NULL;
    if (buf != NULL) {
        free(buf);
    }
    asprintf(&buf, "preset_%d", presetNumber);
    return buf;
}

typedef struct {
    int64_t panPosition, tiltPosition, zoomPosition;
} preset_t;

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

    int panSpeed = onProgram ? 5 : 24;
    int tiltSpeed = onProgram ? 5 : 24;
    int zoomSpeed = getVISCAZoomSpeedFromTallyState();

    cancelRecallIfNeeded("recallPreset");
    bool retval = setPanTiltPosition(preset.panPosition, scaleVISCAPanTiltSpeedToCoreSpeed(panSpeed),
                                        preset.tiltPosition, scaleVISCAPanTiltSpeedToCoreSpeed(tiltSpeed));
    bool retval2 = setZoomPosition(preset.zoomPosition, scaleVISCAZoomSpeedToCoreSpeed(zoomSpeed));

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


#pragma mark - Calibration

double timeStamp(void);

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
      // This happened earlier, but log it.
      fprintf(stderr, "Resetting metrics.\n");
    }

    if (localDebug) {
      fprintf(stderr, "Waiting for the sytem to stabilize.\n");
    }

    // Wait a whole second to ensure everything is up and running.
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
        if (abs(lastPosition[axis] - value) > 5) {
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
        fprintf(stderr, "Loop check: panMoved: %s tiltMoved: %s time: %lf\n",
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
  if (localDebug) {
    fprintf(stderr, "Calibrating motor module.\n");
  }

  motorModuleCalibrate();

  if (localDebug) {
    fprintf(stderr, "Calibrating panasonic module.\n");
  }

  panaModuleCalibrate();

  if (localDebug) {
    fprintf(stderr, "Out of loop.  Writing configuration.\n");
  }
  gCalibrationModeVISCADisabled = false;
  gCalibrationMode = false;
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

bool spinAxis(axis_identifier_t axis, int microseconds, int64_t startPosition, int64_t endPosition,
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
        fprintf(stderr, "Failed spinning (out of bounds)\n");
      }
      return false;
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

  return true;
}

int64_t calibrationValueForMoveAlongAxis(axis_identifier_t axis,
    int64_t startPosition, int64_t endPosition, int speed, float dutyCycle) {
  bool localDebug = false;
  int attempts = 0;
  int64_t motionStartPosition = 0;
  double startTime = 0;
  int64_t motionEndPosition = 1;
  double endTime = 1;

  // If the motor was already moving in the right direction, don't wait as long.
  static bool inMotion = false;

  // If we don't get values because a half second moves too far, set to true.
  bool movedTooFast = false;

  if (localDebug) {
    fprintf(stderr, "Obtaining calibration value for axis %d speed %d (start=%" PRId64
                    ", end=%" PRId64 "\n", axis, speed, startPosition, endPosition);
  }

  static int direction = -1;

  while (attempts++ < 5) {
    if (localDebug) {
      fprintf(stderr, "Setting axis %d to speed %d\n", axis, speed);
    }
    // Set the axis speed using the "raw" function so that there is no scaling involved.  This
    // avoids any precision loss caused by converting from motor speeds to core speeds and back
    // without having to run the motor at all 1,000 core speeds.
    setAxisSpeedRaw(axis, speed * direction, false);

    // Run the motors for a while before computing the speed.
    float dutyCycleMultiplier = (dutyCycle < .25) ? 2 : (dutyCycle < .50) ? 1.5 : 1;
    int delay = ((inMotion || movedTooFast) ? 100000 : 500000) * dutyCycleMultiplier;

    if (spinAxis(axis, delay, startPosition, endPosition, direction)) {
      motionStartPosition = getAxisPosition(axis);
      startTime = timeStamp();

      int duration = 1000000;
      if (spinAxis(axis, duration, startPosition, endPosition, direction)) {
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
    }
    direction = -direction;

    inMotion = false;
    attempts++;
  }
  motionEndPosition = getAxisPosition(axis);
  endTime = timeStamp();

  int64_t distance = abs(motionEndPosition - motionStartPosition);
  double duration = endTime - startTime;

  inMotion = true;
  double distancePerSecond = (int64_t)((double)distance / duration);
  if (localDebug) {
    fprintf(stderr, "Returning distance per second %lf\n", distancePerSecond);
  }
  return distancePerSecond;
}

int64_t *calibrationDataForMoveAlongAxis(axis_identifier_t axis,
                                     int64_t startPosition,
                                     int64_t endPosition,
                                     int32_t min_speed,
                                     int32_t max_speed) {
  bool localDebug = false;
  if (localDebug) {
    fprintf(stderr, "Gathering calibration data for axis %d\n", axis);
  }

  int64_t *data = (int64_t *)malloc(sizeof(int64_t) * (max_speed - min_speed + 1));
  setAxisPositionIncrementally(axis, startPosition, SCALE_CORE);  // Move as quickly as possible.
  waitForAxisMove(axis);

  if (localDebug) {
    fprintf(stderr, "Finished initial move.\n");
  }

  for (int32_t speed = min_speed; speed <= max_speed; speed++) {
    float dutyCycle = (speed - min_speed) / (float)(max_speed - min_speed);

    bool done = false;
    int64_t positionsPerSecond[5];
    int64_t positionsPerSecondAverage = 0;
    while (!done) {
      // No need to invert the drive direction.  The motor driver should already be
      // handling that.
      int64_t min = 0, max = 0;
      for (int i = 0 ; i < 5; i++) {
        positionsPerSecond[i] =
            calibrationValueForMoveAlongAxis(axis, startPosition, endPosition, speed, dutyCycle);
        if (i == 0) {
          min = positionsPerSecond[i];
          max = positionsPerSecond[i];
        } else if (positionsPerSecond[i] > max) {
          max = positionsPerSecond[i];
        } else if (positionsPerSecond[i] < min) {
          min = positionsPerSecond[i];
        }
        fprintf(stderr, "Positions per second at speed %d [%d]: %" PRId64 "\n",
                speed, i, positionsPerSecond[i]);
      }
      if (min == max) {
        positionsPerSecondAverage = min;
        done = true;
      } else {
        int64_t alltotal = 0;
        int64_t total = 0;
        int count = 0;
        int64_t tempmin = min, tempmax = max;
        int64_t newmin = -1, newmax = -1;
        for (int i = 0 ; i < 5; i++) {
          int64_t value = positionsPerSecond[i];

          alltotal += value;
          if (value == tempmin) {
            tempmin = -1;
            continue;
          }
          if (value == tempmax) {
            tempmax = -1;
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
        positionsPerSecondAverage = round((double)total / count);

        double error = (double)newmax - newmin;
        double errorPercent = error / newmax;
        if (error <= 2 || errorPercent < .1) {
          done = true;
        }
      }
    }

    int index = speed - min_speed;
    data[index] = positionsPerSecondAverage;
    fprintf(stderr, "Positions per second at speed %d: %" PRId64 "\n",
            index, positionsPerSecondAverage);

    // The motor may stall at low voltages, but once it gets moving, it should get faster
    // for each increase in voltage.  If not, something went wrong, and our results are invalid.
    if (speed >= 1 && data[index] <= data[index - 1] && data[index] > 0) {
      fprintf(stderr, "Motor slowed down.  Recomputing previous position and current position.\n");
      speed -= 2;
    }
  }
  setAxisSpeedRaw(axis, 0, false);
  if (localDebug) {
    fprintf(stderr, "Done collecting data for axis %d\n", axis);
  }

  return data;
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

int64_t *readCalibrationDataForAxis(axis_identifier_t axis,
                                    int *length) {
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
  if (length) {
    *length = count;
  }
  return data;
}

bool writeCalibrationDataForAxis(axis_identifier_t axis, int64_t *calibrationData, int length) {
  char *stringData = NULL;
  asprintf(&stringData, "%s", "");
  for (int i = 0; i < length; i++) {
    char *previousStringData = stringData;
    asprintf(&stringData, "%s %" PRId64, previousStringData, calibrationData[i]);
    free(previousStringData);
  }
  bool retval = setConfigKey(calibrationDataKeyNameForAxis(axis), stringData + 1);
  free(stringData);
  return retval;
}


#pragma mark - Pan and tilt direction information.

bool resetCalibration(void) {
  bool retval = true;
  retval = removeConfigKey(kPanMotorReversedKey) && retval;
  retval = removeConfigKey(kTiltMotorReversedKey) && retval;
  retval = removeConfigKey(kPanEncoderReversedKey) && retval;
  retval = removeConfigKey(kTiltEncoderReversedKey) && retval;
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


#pragma mark - Tests

void run_startup_tests(void) {
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

  int64_t fakeData[] = { 0, 1, 2, 3 };
  assert(writeCalibrationDataForAxis(30, fakeData, sizeof(fakeData) / sizeof(int64_t)));

  int length;
  int64_t *fakeData2 = readCalibrationDataForAxis(30, &length);
  assert(length == 4);

  for (int i=0; i < 4; i++) {
    assert(fakeData[i] == fakeData2[i]);
  }
}

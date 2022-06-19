// #include <csignal>
// #include <cstddef>
// #include <cstdio>
// #include <atomic>
// #include <chrono>
// #include <string>
// #include <thread>

#include "constants.h"
#include "fakeptz.h"
#include "motorptz.h"
#include "panasonicptz.h"

#include <math.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include <dlfcn.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <pthread.h>

static int kAxisStallThreshold = 100;

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

typedef enum {
  axis_identifier_pan = 0,
  axis_identifier_tilt = 1,
  axis_identifier_zoom = 2,
} axis_identifier_t;

#define NUM_AXES (axis_identifier_zoom + 1)

static bool gAxisMoveInProgress[NUM_AXES];
static int64_t gAxisMoveStartPosition[NUM_AXES];
static int64_t gAxisMoveTargetPosition[NUM_AXES];
static int64_t gAxisMoveMaxSpeed[NUM_AXES];
static int64_t gAxisLastMoveSpeed[NUM_AXES];
static int64_t gAxisPreviousPosition[NUM_AXES];
static int gAxisStalls[NUM_AXES];

int debugPanAndTilt = 0; // kDebugModePan;// kDebugModeZoom;  // Bitmap from debugMode.

pthread_t network_thread;
void *runPTZThread(void *argIgnored);
void *runNetworkThread(void *argIgnored);
bool handleVISCAPacket(visca_cmd_t command, int sock, struct sockaddr *client, socklen_t structLength);
int getVISCAZoomSpeedFromTallyState(void);

bool absolutePositioningSupportedForAxis(axis_identifier_t axis);
void handleRecallUpdates(void);
int64_t getAxisPosition(axis_identifier_t axis);
bool setAxisPositionIncrementally(axis_identifier_t axis, int64_t position, int64_t maxSpeed);
bool setAxisSpeed(axis_identifier_t axis, int64_t position);
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

#pragma mark - Main

int main(int argc, char *argv[]) {

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

  // Spin this thread forever for now.
  while (1) {
    usleep(1000000);
  }
}

#pragma mark - Testing implementation (no-ops)

bool debugSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed) {
  fprintf(stderr, "setPanTiltSpeed: pan speed: %" PRId64 "\n"
                  "tilt speed: %" PRId64 "\n", panSpeed, tiltSpeed);
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

#pragma mark - Generic routines

int actionProgress(int64_t startPosition, int64_t curPosition, int64_t endPosition,
                   int64_t previousPosition, int *stalls) {
  bool localDebug = false;
  int64_t progress = llabs(curPosition - startPosition);
  int64_t total = llabs(endPosition - startPosition);

  if (total == 0) {
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
        // For now, we evenly ramp up to 10% and down from 90%.  This is *NOT* ideal,
        // particularly given that we get zoom position updates only once per second.
        int distance_to_nearest_endpoint = (progress >= 900) ? (1000 - progress) : progress;

        // With vertical acccuracy of ± .1% at the two endpoints (i.e. 0.001 at the bottom,
        //     0.999 at the top):
        // 1 / (1 + e^-x) would give us a curve of length 14 from -7 to 7.
        // 1 / (1 + e^-(x*7)) would give us a curve of length 14 from -1 to 1.
        // 1 / (1 + e^-((x*7 / 50)) would give us a curve of length 100 from -50 to 50.
        // 1 / (1 + e^-(((x - 50)*7 / 50)) gives us a curve from 0 to 100 (one tenth the
        //                                 progress period).
        // 1 / (1 + e^-(7x/50 - 7)) is simplified.
        // 1 / (1 + e^(-7x/50 + 7) is fully simplified.
        double exponent = 7.0 - ((7.0 * distance_to_nearest_endpoint) / 50.0);
        double speedFromProgress = 1 / (1 + pow(M_E, exponent));
        return round(speedFromProgress * 1000.0);
    } else {
        return 1000;
    }
}

int getAxisScale(axis_identifier_t axis) {
    switch(axis) {
        case axis_identifier_pan:
            return SCALE_CORE;
        case axis_identifier_tilt:
            return SCALE_CORE;
        case axis_identifier_zoom:
            return SCALE_CORE;
    }
    return SCALE_CORE;
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
  bool localDebug = false;

  for (axis_identifier_t axis = axis_identifier_pan ; axis < NUM_AXES; axis++) {
    if (gAxisMoveInProgress[axis]) {
      if (localDebug) {
        fprintf(stderr, "UPDATING AXIS %d\n", axis);
      }

      // Compute how far into the motion we are (with a range of 0 to 1,000).
      int direction = (gAxisMoveTargetPosition[axis] > gAxisMoveStartPosition[axis]) ? 1 : -1;
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
        setAxisSpeed(axis, 0);
      } else {      
        // Scale based on max speed out of 24 (VISCA percentage) and scale to a range of -511 to 511.
        int axis_scale = getAxisScale(axis);
        setAxisSpeed(axis, computeSpeed(panProgress) * direction);
      }
    } else if (localDebug) {
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

bool setAxisSpeed(axis_identifier_t axis, int64_t speed) {
  gAxisLastMoveSpeed[axis] = speed;
  switch(axis) {
    case axis_identifier_pan:
        return SET_PAN_TILT_SPEED(speed, gAxisLastMoveSpeed[axis_identifier_tilt]);
    case axis_identifier_tilt:
        return SET_PAN_TILT_SPEED(gAxisLastMoveSpeed[axis_identifier_pan], speed);
    case axis_identifier_zoom:
        return SET_ZOOM_SPEED(speed);
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
  fprintf(stderr, "RECALL CANCELLED (%s)\n", context);
  for (axis_identifier_t axis = axis_identifier_pan; axis < NUM_AXES; axis++) {
    gAxisMoveInProgress[axis] = false;
  }
}

bool setAxisPositionIncrementally(axis_identifier_t axis, int64_t position, int64_t maxSpeed) {
  bool localDebug = true;
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
          fprintf(stderr, "Error\n");
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
          if (command[3] == 0x01 && command[4] == 0x0a && command[6] == 0xff) {
            while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
            int tallyState = GET_TALLY_STATE();
            visca_response_t *response = NULL;
            if (command[5] == 0x00) {
              // 8x 09 7E 01 0A 00 FF -> y0 50 0p FF
              response = tallyEnabledResponse(tallyState);
            } else if (command[5] == 0x01) {
              // 8x 09 7E 01 0A 01 FF -> y0 50 0p FF
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
                    setAxisSpeed(axis_identifier_zoom, scaleVISCAZoomSpeedToCoreSpeed(zoomSpeed));
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
                    setAxisSpeed(axis_identifier_pan, scaleVISCAPanTiltSpeedToCoreSpeed(panSpeed));
                    setAxisSpeed(axis_identifier_tilt, scaleVISCAPanTiltSpeedToCoreSpeed(tiltSpeed));
                }

                while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                return true;
}
              case 0x02: // Pan/tilt absolute
{
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
        default:
          break;
      }
    case 0x0A: // Unimplemented
      // 0x81 0a 01 03 10 ff : AF calibration.
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
        fprintf(stderr, "Saving preset %d\n", presetNumber);
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

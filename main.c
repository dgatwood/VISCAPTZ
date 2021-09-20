// #include <csignal>
// #include <cstddef>
// #include <cstdio>
// #include <atomic>
// #include <chrono>
// #include <string>
// #include <thread>

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

#pragma mark - Configuration

#define USE_PANASONIC_PTZ

#ifdef USE_PANASONIC_PTZ
#include "panasonicptz.h"
#else
#include "fakeptz.h"
#endif

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
  axis_identifier_pan = 1,
  axis_identifier_tilt = 2,
  axis_identifier_zoom = 4
} axis_identifier_t;

pthread_t network_thread;
void *runPTZThread(void *argIgnored);
void *runNetworkThread(void *argIgnored);
bool handleVISCAPacket(visca_cmd_t command, int sock, struct sockaddr *client, socklen_t structLength);

visca_response_t *failedVISCAResponse(void);
visca_response_t *enqueuedVISCAResponse(void);
visca_response_t *completedVISCAResponse(void);


#pragma mark - Main

int main(int argc, char *argv[]) {
  pthread_create(&network_thread, NULL, runNetworkThread, NULL);

  SET_IP_ADDR("127.0.0.1");

  if (!MODULE_INIT()) {
    fprintf(stderr, "Module init failed.  Bailing.\n");
    exit(1);
  }

  // Spin this thread forever for now.
  while (1) {
    usleep(1000000);
  }
}

#pragma mark - Testing implementation (no-ops)

bool debugSetPanSpeed(double speed) {
  fprintf(stderr, "setPanSpeed: %lf\n", speed);
  return false;
}

bool debugSetTiltSpeed(double speed) {
  fprintf(stderr, "setTiltSpeed: %lf\n", speed);
  return false;
}

bool debugSetZoomSpeed(double speed) {
  fprintf(stderr, "setZoomSpeed: %lf\n", speed);
  return false;
}

double debugGetPanPosition(void) {
  // Future versions will return a position (arbitrary scale).
  // Return 0 if the value is "close enough" that motion is impossible.
  fprintf(stderr, "getPanPosition\n");
  return 0.0;
}

double debugGetTiltPosition(void) {
  // Future versions will return a position (arbitrary scale).
  // Return 0 if the value is "close enough" that motion is impossible.
  fprintf(stderr, "getTiltPosition\n");
  return 0.0;
}

double debugGetZoomPosition(void) {
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

double speedForMotion(double fromPosition, double toPosition, axis_identifier_t axis) {
  switch(axis) {
    case axis_identifier_pan:
        return GET_PAN_POSITION();
    case axis_identifier_tilt:
        return GET_TILT_POSITION();
    case axis_identifier_zoom:
        return GET_ZOOM_POSITION();
  }
}

double getAxisPosition(axis_identifier_t axis) {
  switch(axis) {
    case axis_identifier_pan:
        return GET_PAN_POSITION();
    case axis_identifier_tilt:
        return GET_TILT_POSITION();
    case axis_identifier_zoom:
        return GET_ZOOM_POSITION();
  }
  return 0.0;
}

bool setAxisSpeed(axis_identifier_t axis, double position) {
  switch(axis) {
    case axis_identifier_pan:
        return SET_PAN_SPEED(position);
    case axis_identifier_tilt:
        return SET_TILT_SPEED(position);
    case axis_identifier_zoom:
        return SET_ZOOM_SPEED(position);
  }
  return false;
}

bool setAxisPosition(axis_identifier_t axis, double position, double maxSpeed) {
  double currentPosition = getAxisPosition(axis);
  double speed = speedForMotion(currentPosition, position, axis);
  if (speed < 0) {
    while (currentPosition > position) {
      if (!setAxisSpeed(axis, speed)) return false;
      currentPosition = getAxisPosition(axis);
    }
  } else if (speed > 0) {
    while (currentPosition < position) {
      if (!setAxisSpeed(axis, speed)) return false;
      currentPosition = getAxisPosition(axis);
    }
  }
  return true;
}

bool setPanPosition(double position, double maxSpeed) {
  fprintf(stderr, "setPanPosition: %lf, %lf\n", position, maxSpeed);
  if (!PAN_AND_TILT_POSITION_SUPPORTED) {
    return false;
  }
  return setAxisPosition(axis_identifier_pan, position, maxSpeed);
}

bool setTiltPosition(double position, double maxSpeed) {
  fprintf(stderr, "setTiltPosition: %lf, %lf\n", position, maxSpeed);
  if (!PAN_AND_TILT_POSITION_SUPPORTED) {
    return false;
  }
  return setAxisPosition(axis_identifier_tilt, position, maxSpeed);
}

// Range 0x0000 to 0xffff
bool setZoomPositionIncrementally(double position, double maxSpeed) {
  fprintf(stderr, "setZoomPositionIncrementally: %lf, %lf\n", position, maxSpeed);
  if (!ZOOM_POSITION_SUPPORTED) {
    return false;
  }
  return setAxisPosition(axis_identifier_zoom, position, maxSpeed);
}

#pragma mark - Networking

bool sendVISCAResponse(visca_response_t *response, uint32_t sequenceNumber, int sock, struct sockaddr *client, socklen_t structLength);

void *runNetworkThread(void *argIgnored) {
  // Listen on UDP port 1259.
  short port = 1259;

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
                int8_t zoomRawSpeed = command[4] & 0xf;
                int8_t zoomSpeed = ((zoomCmd & 0xf0) == 0x20) ? zoomRawSpeed : -zoomRawSpeed;
                SET_ZOOM_SPEED(ZOOM_SPEED_SCALE(zoomSpeed));
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
              case 0x47: // Absolute zoom.
{
                while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
                uint32_t position = ((command[4] & 0xf) << 12) | ((command[5] & 0xf) << 8) |
                                    ((command[6] & 0xf) << 4) | (command[7] & 0xf);
                uint8_t speed = ((command[len - 2] & 0xf0) == 0) ? (command[8] & 0xf) : 3;
                SET_ZOOM_POSITION(position, ZOOM_SPEED_SCALE(speed));

                // If (command[9] & 0xf0) == 0, then the low bytes of 9-12 are focus position,
                // and speed is at position 13, shared with focus.  If we ever add support for
                // focusing, handle that case here.

                while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                return true;
}
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

                SET_TILT_SPEED(tiltSpeed);
                SET_PAN_SPEED(panSpeed);

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
                int16_t panValue = (int16_t)rawPanValue;
                int16_t tiltValue = (int16_t)rawTiltValue;

                setTiltPosition(tiltValue, TILT_SPEED_SCALE(tiltSpeed));
                setPanPosition(tiltValue, PAN_SPEED_SCALE(panSpeed));

                while (!sendVISCAResponse(completedVISCAResponse(), sequenceNumber, sock, client, structLength));
                return true;
}
              case 0x03: // Pan/tilt relative
{
                while (!sendVISCAResponse(enqueuedVISCAResponse(), sequenceNumber, sock, client, structLength));
                int16_t panSpeed = command[4];
                int16_t tiltSpeed = command[5];
                uint16_t rawPanValue = ((command[6] & 0xf) << 12) | ((command[7] & 0xf) << 8) |
                                       ((command[8] & 0xf) << 4) | (command[9] & 0xf);
                uint16_t rawTiltValue = ((command[10] & 0xf) << 12) | ((command[11] & 0xf) << 8) |
                                        ((command[12] & 0xf) << 4) | (command[13] & 0xf);
                int16_t panValue = (int16_t)rawPanValue;
                int16_t tiltValue = (int16_t)rawTiltValue;

                double panPosition = GET_PAN_POSITION() + panValue;
                double tiltPosition = GET_TILT_POSITION() + tiltValue;
                setTiltPosition(tiltPosition, TILT_SPEED_SCALE(tiltSpeed));
                setPanPosition(tiltPosition, PAN_SPEED_SCALE(panSpeed));

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

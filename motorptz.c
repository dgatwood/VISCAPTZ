#include "motorptz.h"

#define ENABLE_MOTOR_HARDWARE 1
#define ENABLE_ENCODER_HARDWARE 1

#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>

#include "main.h"
#include "constants.h"

#define ENABLE_STATUS_DEBUGGING 0

#if ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE

#include "motorcontrol/lib/Config/DEV_Config.h"
#include "motorcontrol/lib/Config/Debug.h"
#include "motorcontrol/lib/MotorDriver/MotorDriver.h"
#include "motorcontrol/lib/PCA9685/PCA9685.h"
// #include "motorcontrol/DEV_Config.c"
// #include "motorcontrol/MotorDriver.c"
// #include "motorcontrol/PCA9685.c"

#endif  // ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE

// For printing zoom speed.
#include "panasonicptz.h"

#if ENABLE_HARDWARE && USE_CANBUS
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif  // ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE


// Figure out these values when we have the hardware.  They will probably
// have to be configurable, too.
#define MIN_ENCODER_POSITION_FOR_TILT 3000
#define MAX_ENCODER_POSITION_FOR_TILT 20000
#define ENCODER_SCALE_FOR_TILT (MAX_ENCODER_POSITION_FOR_TILT - MIN_ENCODER_POSITION_FOR_TILT)
#define MIN_ENCODER_POSITION_FOR_PAN 3000
#define MAX_ENCODER_POSITION_FOR_PAN 20000
#define ENCODER_SCALE_FOR_PAN (MAX_ENCODER_POSITION_FOR_PAN - MIN_ENCODER_POSITION_FOR_PAN)

static bool motor_enable_debugging = false;


#pragma mark - Motor pan/tilt implementation

pthread_t motor_control_thread;
pthread_t position_monitor_thread;
void *runMotorControlThread(void *argIgnored);
void *runPositionMonitorThread(void *argIgnored);

bool motorPanTiltPositionEnabled = false;

static char *g_cameraIPAddr = NULL;

static volatile int64_t g_pan_speed = 0;
static volatile int64_t g_tilt_speed = 0;

static volatile int64_t g_last_pan_position = 0;
static volatile int64_t g_last_tilt_position = 0;

static volatile bool g_pan_tilt_raw = false;

bool motorModuleInit(void) {
    bool localDebug = motor_enable_debugging || false;
    if (localDebug) fprintf(stderr, "Initializing motor module\n");
#if ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE
    if (localDebug) fprintf(stderr, "Initializing dev motor module\n");
    if (DEV_ModuleInit()) {
      return false;
    }
    if (localDebug) fprintf(stderr, "Initializing motor\n");
    Motor_Init();
#endif  // ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE
    if (localDebug) fprintf(stderr, "Motor initialized\n");

  // Start the motor control thread in the background.
  if (motor_enable_debugging) fprintf(stderr, "Motor module init\n");
  pthread_create(&motor_control_thread, NULL, runMotorControlThread, NULL);
#if ENABLE_ENCODER_HARDWARE
  pthread_create(&position_monitor_thread, NULL, runPositionMonitorThread, NULL);
#endif

  if (localDebug) fprintf(stderr, "Motor module init done\n");
  return true;
}

bool motorSetIPAddress(char *address) {
  if (g_cameraIPAddr != NULL) {
    free(g_cameraIPAddr);
  }
  asprintf(&g_cameraIPAddr, "%s", address);
  return true;
}

bool motorSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed, bool isRaw) {
  g_pan_tilt_raw = isRaw;
  g_pan_speed = panSpeed;
  g_tilt_speed = tiltSpeed;
  return true;
}

bool motorGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition) {
  if (panPosition != NULL) {
    *panPosition = g_last_pan_position;
  }
  if (tiltPosition != NULL) {
    *tiltPosition = g_last_tilt_position;
  }
  return true;
}

#pragma mark - Position monitor thread

#if ENABLE_ENCODER_HARDWARE
#if USE_CANBUS
int motorOpenCANSock(void) {

    system("sudo ifconfig can0 down");
    system("sudo ip link set can0 type can bitrate 500000");
    system("sudo ifconfig can0 up");

    bool localDebug = false;
    if (localDebug) fprintf(stderr, "Opening CANBus socket... ");
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket PF_CAN failed");
        return 1;
    }
    
    struct ifreq interfaceRequest;
    strcpy(interfaceRequest.ifr_name, "can0");
    int retval = ioctl(sock, SIOCGIFINDEX, &interfaceRequest);
    if (retval < 0) {
        perror("ioctl failed");
        return -1;
    }

    struct sockaddr_can sockaddr;
    sockaddr.can_family = AF_CAN;
    sockaddr.can_ifindex = interfaceRequest.ifr_ifindex;
    retval = bind(sock, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (retval < 0) {
        perror("bind failed");
        return -1;
    }
    
    if (localDebug) fprintf(stderr, "done\n");
    return sock;
}

bool updatePositionsCANBus(int sock);
void resetCenterPositionsCANBus(int sock);

#else  // !USE_CANBUS

int motorOpenSerialDev(char *path) {
  int fd = open(path, O_RDWR);

  if (fd < 0) return fd;

  struct termios serial_port_settings;
  int retval = tcgetattr(fd, &serial_port_settings);
  if (retval < 0) {
    perror("Failed to get termios structure");
    exit(2);
  }
  retval = cfsetospeed(&serial_port_settings, B9600);
  if (retval < 0) {
    perror("Failed to set 9600 output baud rate");
    exit(3);
  }
  retval = cfsetispeed(&serial_port_settings, B9600);
  if (retval < 0) {
    perror("Failed to set 9600 input baud rate");
    exit(4);
  }
  retval = tcsetattr(fd, TCSANOW, &serial_port_settings);
  if (retval < 0) {
    perror("Failed to set serial attributes");
    exit(5);
  }
  return fd;
}

void updatePositionsSerial(int pan_fd, int tilt_fd);
void resetCenterPositionsSerial(int tilt_fd, int pan_fd);

#endif  // USE_CANBUS

void *runPositionMonitorThread(void *argIgnored) {
    bool localDebug = false;
#if ENABLE_HARDWARE
  #if USE_CANBUS
    int sock = motorOpenCANSock();
  #else  // !USE_CANBUS
    int tilt_fd = motorOpenSerialDev(SERIAL_DEV_FILE_FOR_TILT);
    int pan_fd = motorOpenSerialDev(SERIAL_DEV_FILE_FOR_PAN);

    if (pan_fd < 0 || tilt_fd < 0) {
      fprintf(stderr, "Could not open serial ports.  Disabling position monitoring.\n");
      return NULL;
    }
  #endif  // USE_CANBUS
#endif  // ENABLE_HARDWARE

  // During calibration, we set the current position to be the midpoint of the encoders,
  // to minimize the chances of going off-scale low/high, because this software makes
  // no attempt at understanding wraparound right now.
  if (gCalibrationMode) {
  #if USE_CANBUS
    resetCenterPositionsCANBus(sock);
  #else
    resetCenterPositionsSerial(tilt_fd);
    resetCenterPositionsSerial(pan_fd);
  #endif
  }

  // Read the position.
  while (1) {
#if ENABLE_HARDWARE
    #if USE_CANBUS
        if (!updatePositionsCANBus(sock)) {
            if (localDebug) fprintf(stderr, "Reopening socket after failure.\n");
            sock = motorOpenCANSock();
        }
    #else  // !USE_CANBUS
        updatePositionsSerial(pan_fd, tilt_fd)
        usleep(10000);  // Update 100x per second (latency-critical).
    #endif  // USE_CANBUS
#else  // !ENABLE_HARDWARE
    g_last_pan_position += g_pan_speed;
    g_last_tilt_position += g_tilt_speed;
    usleep(10000);  // Update 100x per second (latency-critical).
#endif  // ENABLE_HARDWARE
  }
#if ENABLE_HARDWARE
  #if USE_CANBUS
    close(sock);
    system("sudo ifconfig can0 down");
  #else  // !USE_CANBUS
    close(pan_fd);
    close(tilt_fd);
  #endif  // USE_CANBUS
#endif  // ENABLE_HARDWARE
  return NULL;
}

#if USE_CANBUS
void handleCANFrame(struct can_frame *frame);
bool sendCANRequestFrame(int sock, uint8_t deviceID);

bool updatePositionsCANBus(int sock) {
    bool localDebug = false;
    bool gotRead = false;
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 10 reads per second.

    int retval = select(sock + 1, &readfds, NULL /* &writefds */, NULL, &tv);
    if (retval > 0) {
        if (FD_ISSET(sock, &readfds)) {
            struct can_frame frame;
            ssize_t bytesRead = read(sock, &frame, sizeof(frame));
            if (bytesRead != sizeof(frame)) {
                if (localDebug) fprintf(stderr, "CANBus packet read failed.  (Expected %zu got %zu)\n", sizeof(frame), bytesRead);
                close(sock);
                return false;
            }
            if (localDebug) fprintf(stderr, "Reading CANBus packet\n");
            handleCANFrame(&frame);
            gotRead = true;
            if (localDebug) fprintf(stderr, "Done reading CANBus packet\n");
        }
    } else if (retval < 0) {
        perror("motorptz");
        fprintf(stderr, "Select returned -1\n");
    }
    if (!gotRead) {
        if (localDebug) fprintf(stderr, "Timed out reading CANBus packet.  Requesting data.\n");
        if (localDebug) fprintf(stderr, "Sending data request.\n");
        if (!sendCANRequestFrame(sock, panCANBusID)) {
            if (localDebug) fprintf(stderr, "CANBus packet write failed.\n");
            close(sock);
            return false;
        }
        if (!sendCANRequestFrame(sock, tiltCANBusID)) {
            if (localDebug) fprintf(stderr, "CANBus packet write failed.\n");
            close(sock);
            return false;
        }
        if (localDebug) fprintf(stderr, "Done sending CANBus packet\n");
    }
    return true;
}

struct can_frame canBusFrameMake(uint32_t can_id, uint8_t can_dlc, uint8_t *data) {
  struct can_frame message;
  memset(&message, 0, sizeof(struct can_frame));
  message.can_id = can_id;
  message.can_dlc = can_dlc;
  memcpy(message.data, data, sizeof(message.data));
  return message;
}

void handleCANFrame(struct can_frame *response) {
    bool localDebug = false;
    if (localDebug) fprintf(stderr, "Processing CAN frame.\n");
    if (response->data[0] == 0x7 && response->data[2] == 0x1) {
        long value = response->data[3] | (response->data[4] << 8) |
                     (response->data[5] << 16) | (response->data[6] << 24);
        if (response->data[1] == panCANBusID) {
            if (localDebug) fprintf(stderr, "Got pan: %ld.\n", value);
            g_last_pan_position = value;
        } else if (response->data[1] == tiltCANBusID) {
            if (localDebug) fprintf(stderr, "Got tilt: %ld.\n", value);
            g_last_tilt_position = value;
        } else {
            if (localDebug) fprintf(stderr, "Received message from unknown CAN bus ID %d", response->data[1]);
        }
    } else {
        fprintf(stderr, "Unknown response: %02x %02x %02x %02x %02x %02x %02x %02x from device %lu with length code %d\n",
                        response->data[0], response->data[1], response->data[2], response->data[3],
                        response->data[4], response->data[5], response->data[6], response->data[7],
                        (unsigned long)response->can_id, response->can_dlc);
    }
}

bool sendCANRequestFrame(int sock, uint8_t deviceID) {
    bool localDebug = false;
    uint8_t data[8] = { 0x04, deviceID, 0x01, 0, 0, 0, 0, 0 };
    struct can_frame message = canBusFrameMake(deviceID, 4, data);

    if (localDebug) {
        fprintf(stderr, "Writing to socket %d frame %d %d\n", sock, message.can_id, message.can_dlc);
    }

    ssize_t bytesWritten = write(sock, &message, sizeof(message));
    if (bytesWritten != sizeof(message)) {
        fprintf(stderr, "CANBus write failed (expected length %zu, got %zu)\n", sizeof(message), bytesWritten);
        return false;
    }
    return true;
}

void reassign_encoder_device_id(int oldCANBusID, int newCANBusID) {
    int sock = motorOpenCANSock();
    uint8_t data[8] = { 0x04, oldCANBusID, 0x02, newCANBusID, 0, 0, 0, 0 };
    struct can_frame message = canBusFrameMake(oldCANBusID, 4, data);

    fprintf(stderr, "Reassigning device %d to %d\n", oldCANBusID, newCANBusID);
    if (write(sock, &message, sizeof(message)) == sizeof(message)) {
        struct can_frame response;
        if (read(sock, &response, sizeof(response)) == sizeof(response)) {
            if (response.data[0] == 0x4 && response.data[1] == oldCANBusID &&
                response.data[2] == 0x2 && response.data[3] == 0 &&
                response.can_id == newCANBusID) {
                    fprintf(stderr, "Reassignment successful.");
                    exit(0);
            } else {
                fprintf(stderr, "Reassignment failed with error %d\n", response.data[3]);
                exit(1);
            }
        } else {
            fprintf(stderr, "Reassignment failed (socket read).\n");
        }
    } else {
        fprintf(stderr, "Reassignment failed (socket write).\n");
        exit(1);
    }
}

void resetCenterPositionOfCANBusEncoder(int sock, int CANBusID) {
    uint8_t data[8] = { 0x04, CANBusID, 0x0C, 0x01, 0, 0, 0, 0 };
    struct can_frame message = canBusFrameMake(CANBusID, 4, data);

    fprintf(stderr, "Setting the midpint of the encoder to the current position.\n");
    if (write(sock, &message, sizeof(message)) == sizeof(message)) {
        struct can_frame response;
        if (read(sock, &response, sizeof(response)) == sizeof(response)) {
            if (response.data[0] == 0x4 && response.data[1] == CANBusID &&
                response.data[2] == 0xC && response.data[3] == 0) {
                    fprintf(stderr, "Reset successful.");
            } else {
                fprintf(stderr, "Reset failed with error %d\n", response.data[3]);
                exit(1);
            }
        } else {
            fprintf(stderr, "Reset failed (socket read).\n");
            exit(1);
        }
    } else {
        fprintf(stderr, "Reset failed (socket write).\n");
        exit(1);
    }
}

void resetCenterPositionsCANBus(int sock) {
  resetCenterPositionOfCANBusEncoder(sock, panCANBusID);
  resetCenterPositionOfCANBusEncoder(sock, tiltCANBusID);
}


#else  // !USE_CANBUS
void updatePositionsSerial(int pan_fd, int tilt_fd) {
    static const uint8_t requestBuf[] =
        { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0a };
    const uint8_t responseBuf[5];

    // Read the pan position.
    write(pan_fd, requestBuf, sizeof(requestBuf));
    read(pan_fd, (void *)responseBuf, sizeof(responseBuf));
    uint16_t pan_position = ((uint16_t)(responseBuf[3]) << 8) | responseBuf[4];

    // Read the tilt position.
    write(tilt_fd, requestBuf, sizeof(requestBuf));
    read(tilt_fd, (void *)responseBuf, sizeof(responseBuf));
    uint16_t tilt_position = ((uint16_t)(responseBuf[3]) << 8) | responseBuf[4];

    g_last_pan_position = pan_position;

    g_last_tilt_position = tilt_position;
}

void resetCenterPositionsSerial(int tilt_fd, int pan_fd) {
    // Register 0x000E: Write 0x0001.  (Function support code 0x06)
    // [Device ID = 1] 06 00 01 00 0E [CRC high] [CRC low]

    static const uint8_t requestBuf[] = { 0x01, 0x06, 0x00, 0x01, 0x00, 0x0E, 0xCE, 0x59 };
    const uint8_t responseBuf[5];

    write(pan_fd, requestBuf, sizeof(requestBuf));
    read(pan_fd, (void *)responseBuf, sizeof(responseBuf));
    assert(responseBuf[0] == 1);
    assert(responseBuf[1] == 0x10);

    write(tilt_fd, requestBuf, sizeof(requestBuf));
    read(tilt_fd, (void *)responseBuf, sizeof(responseBuf));
    assert(responseBuf[0] == 1);
    assert(responseBuf[1] == 0x10);
}

#endif  // USE_CANBUS
#else  // !ENABLE_ENCODER_HARDWARE
void reassign_encoder_device_id(int oldCANBusID, int newCANBusID) {
}
#endif  // ENABLE_ENCODER_HARDWARE

#pragma mark - Motor control thread

void *runMotorControlThread(void *argIgnored) {
  bool localDebug = false;
  while (1) {
    int scaledPanSpeed = g_pan_tilt_raw ?
        abs(g_pan_speed) :
        abs(scaleSpeed(g_pan_speed, SCALE_CORE, PAN_TILT_SCALE_HARDWARE));
    int scaledTiltSpeed = g_pan_tilt_raw ?
        abs(g_tilt_speed) :
        abs(scaleSpeed(g_tilt_speed, SCALE_CORE, PAN_TILT_SCALE_HARDWARE));

#if (ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE)
    if (localDebug) fprintf(stderr, "Setting motor A speed to %d.\n", scaledPanSpeed);
    Motor_Run(MOTORA, g_pan_speed > 0 ? FORWARD : BACKWARD, scaledPanSpeed);
    if (localDebug) fprintf(stderr, "Setting motor B speed to %d.\n", scaledTiltSpeed);
    Motor_Run(MOTORB, g_tilt_speed > 0 ? FORWARD : BACKWARD, scaledTiltSpeed);
    if (localDebug) fprintf(stderr, "Done.\n");
#endif  // ENABLE_HARDWARE

#if ENABLE_STATUS_DEBUGGING
    int64_t zoom_speed = GET_ZOOM_SPEED();
    int64_t zoom_position = GET_ZOOM_POSITION();

    static int count = 0;
    if (!(count++ % 50)) {
        printf("PAN SPEED: %" PRId64 " (%d) TILT SPEED: %" PRId64 " (%d) "
               "PAN POSITION: %" PRId64 " TILT POSITION: %" PRId64
               " ZOOM SPEED: %" PRId64 " ZOOM POSITION: %" PRId64 "\n",
               g_pan_speed, scaledPanSpeed, g_tilt_speed, scaledTiltSpeed,
               g_last_pan_position, g_last_tilt_position, zoom_speed, zoom_position);
    }
#endif
    usleep(10000);  // Update 100x per second (latency-critical).
  }
  return NULL;
}

#pragma mark - Calibration

void motorModuleCalibrate(void) {
  int64_t leftLimit = leftPanLimit();
  int64_t rightLimit = rightPanLimit();
  int64_t topLimit = topTiltLimit();
  int64_t bottomLimit = bottomTiltLimit();

  int64_t *panCalibrationData = calibrationDataForMoveAlongAxis(
      axis_identifier_pan, rightLimit, leftLimit, 0, PAN_TILT_SCALE_HARDWARE);
  int64_t *tiltCalibrationData = calibrationDataForMoveAlongAxis(
      axis_identifier_tilt, bottomLimit, topLimit, 0, PAN_TILT_SCALE_HARDWARE);

  writeCalibrationDataForAxis(axis_identifier_pan, panCalibrationData, PAN_TILT_SCALE_HARDWARE + 1);
  writeCalibrationDataForAxis(axis_identifier_tilt, tiltCalibrationData, PAN_TILT_SCALE_HARDWARE + 1);
}

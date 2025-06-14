#include "motorptz.h"

#define ENABLE_MOTOR_HARDWARE 1
#define ENABLE_ENCODER_HARDWARE 1

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include "main.h"
#include "configurator.h"
#include "constants.h"

#define ENABLE_STATUS_DEBUGGING 0

#if ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE

#include "motorcontrol/lib/Config/DEV_Config.h"
#include "motorcontrol/lib/Config/Debug.h"
#include "motorcontrol/lib/MotorDriver/MotorDriver.h"
#include "motorcontrol/lib/PCA9685/PCA9685.h"

#endif  // ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE

// For printing zoom speed.
#include "panasonicptz.h"
#include "p2protocol.h"

#if ENABLE_HARDWARE && USE_CANBUS
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif  // ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE


static bool motor_enable_debugging = false;

#if ENABLE_ENCODER_HARDWARE && ENABLE_HARDWARE
  #if USE_CANBUS
    int motorOpenCANSock(void);
    bool updatePositionsCANBus(int sock);
    void resetCenterPositionsCANBus(int sock);
    void handleCANFrame(struct can_frame *frame);
    bool sendCANRequestFrame(int sock, uint8_t deviceID);
  #else
    void updatePositionsSerial(int pan_fd, int tilt_fd);
    void resetCenterPositionsSerial(int tilt_fd, int pan_fd);
  #endif
#endif

pthread_t motor_control_thread;
pthread_t position_monitor_thread;
void *runMotorControlThread(void *argIgnored);
void *runPositionMonitorThread(void *argIgnored);

static volatile int64_t g_pan_speed = 0;
static volatile int64_t g_tilt_speed = 0;

static volatile int64_t g_last_pan_position = 0;
static volatile int64_t g_last_tilt_position = 0;

static volatile bool g_pan_tilt_raw = false;

const char *kMotorsAreSwappedKey = "motors_are_swapped";

#if USE_MOTOR_PAN_AND_TILT
  int64_t *motor_pan_data = NULL;
  int32_t *motor_pan_scaled_data = NULL;
  int64_t *motor_tilt_data = NULL;
  int32_t *motor_tilt_scaled_data = NULL;
#endif  // USE_MOTOR_PAN_AND_TILT


#pragma mark - Motor module initialization

// Public function.  Docs in header.
//
// Initializes the motor control/encoder module.
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
  #else
    // Start the fake hardware in the middle.
    g_last_pan_position = 1000000;
    g_last_tilt_position = 1000000;
  #endif  // ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE
  if (localDebug) fprintf(stderr, "Motor initialized\n");
  return true;
}

bool motorModuleStart(void) {
  bool localDebug = motor_enable_debugging || false;
  // Start the motor control thread in the background.
  if (motor_enable_debugging) fprintf(stderr, "Motor module init\n");
  pthread_create(&motor_control_thread, NULL, runMotorControlThread, NULL);

  #if ENABLE_ENCODER_HARDWARE && ENABLE_HARDWARE
    pthread_create(&position_monitor_thread, NULL, runPositionMonitorThread, NULL);
  #endif  // !(ENABLE_ENCODER_HARDWARE && ENABLE_HARDWARE)

  if (localDebug) fprintf(stderr, "Motor module init done\n");
  return motorModuleReload();
}

// Public function.  Docs in header.
//
// Reinitializes the motor control/encoder module after calibration.
bool motorModuleReload(void) {
  #if USE_MOTOR_PAN_AND_TILT
    int maxSpeed = 0;
    motor_pan_data =
        readCalibrationDataForAxis(axis_identifier_pan, &maxSpeed);
    if (maxSpeed == PAN_TILT_SCALE_HARDWARE) {
        motor_pan_scaled_data =
            convertSpeedValues(motor_pan_data, PAN_TILT_SCALE_HARDWARE,
                               axis_identifier_pan);
    }

    motor_tilt_data =
        readCalibrationDataForAxis(axis_identifier_tilt, &maxSpeed);
    if (maxSpeed == PAN_TILT_SCALE_HARDWARE) {
        motor_tilt_scaled_data =
            convertSpeedValues(motor_tilt_data, PAN_TILT_SCALE_HARDWARE,
                               axis_identifier_tilt);
    }
  #endif  // USE_MOTOR_PAN_AND_TILT
  return true;
}


#pragma mark - Motor pan/tilt implementation

// Public function.  Docs in header.
//
// Sets the pan and tilt speeds.  The actual speed setting is handled by
// the motor control thread (runMotorControlThread).  This just updates
// the global variables that control the speed.
//
// This design ensures that the main control code never gets blocked by
// the hardware drivers and ensures that all changes to hardware speed
// happen in a single thread.
bool motorSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed, bool isRaw) {
  g_pan_tilt_raw = isRaw;
  g_pan_speed = panSpeed;
  g_tilt_speed = tiltSpeed;
  return true;
}

// Gets the pan and tilt positions from the encoders.  The code that
// actually obtains these values from the encoder hardware is part of
// the position monitor thread (runPositionMonitorThread).  This code
// just retrieves the values previously stored into global variables
// by the position monitor thread.
//
// This design ensures that the main control code never gets blocked
// by the hardware drivers, and ensures that the encoders don't get
// confused by requests from multiple threads overlapping.
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

#if ENABLE_ENCODER_HARDWARE && ENABLE_HARDWARE

/**
 * The main loop of the position monitor thread.
 *
 * This function polls the encoder hardware 500x per second.
 */
void *runPositionMonitorThread(void *argIgnored) {
#if ENABLE_HARDWARE
    bool localDebug = false;
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
  if (gRecenter || (gCalibrationMode && !gCalibrationModeQuick)) {
#if ENABLE_HARDWARE
  #if USE_CANBUS
    resetCenterPositionsCANBus(sock);
  #else  // !USE_CANBUS
    resetCenterPositionsSerial(tilt_fd, pan_fd);
    resetCenterPositionsSerial(pan_fd);
  #endif  // USE_CANBUS
#endif  // ENABLE_HARDWARE
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
    #endif  // USE_CANBUS
#endif  // ENABLE_HARDWARE
    usleep(2000);  // Update 500x per second (latency-critical).
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


#pragma mark - CANBus-specific encoder implementation

#if USE_CANBUS

/** Opens a CANBus socket for talking to the position encoders. */
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

/** Updates the current encoder positions (CANBus version). */
bool updatePositionsCANBus(int sock) {
    bool localDebug = false;
    bool gotRead = false;
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // give up if the encoder doesn't respond within 0.1 seconds.

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

/** Creates a CANBus frame with the specified ID, DLC (length), and fixed-length data array. */
struct can_frame CANBusFrameMake(uint32_t can_id, uint8_t can_dlc, uint8_t *data) {
  struct can_frame message;
  memset(&message, 0, sizeof(struct can_frame));
  message.can_id = can_id;
  message.can_dlc = can_dlc;
  memcpy(message.data, data, sizeof(message.data));
  return message;
}

/** Processes a CANBus response from the encoder. */
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

/** Sends a CANBus position response to the encoder. */
bool sendCANRequestFrame(int sock, uint8_t deviceID) {
    bool localDebug = false;
    uint8_t data[8] = { 0x04, deviceID, 0x01, 0, 0, 0, 0, 0 };
    struct can_frame message = CANBusFrameMake(deviceID, 4, data);

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

// Public function.  Docs in header.
//
// Reassigns a CANBus-based encoder to use a new device ID.  This reassignment
// is permanent until updated.  When you get a new set of encoders, you must
// reassign one of them, because they typically all start out using the same
// device ID.  You can do this by connecting a single encoder and the using
// the --reassign command-line flag.  See the main README for details.
void reassign_encoder_device_id(int oldCANBusID, int newCANBusID) {
    int sock = motorOpenCANSock();
    uint8_t data[8] = { 0x04, oldCANBusID, 0x02, newCANBusID, 0, 0, 0, 0 };
    struct can_frame message = CANBusFrameMake(oldCANBusID, 4, data);

    fprintf(stderr, "Reassigning device %d to %d\n", oldCANBusID, newCANBusID);
    if (write(sock, &message, sizeof(message)) == sizeof(message)) {
        struct can_frame response;
        if (read(sock, &response, sizeof(response)) == sizeof(response)) {
            if (response.data[0] == 0x4 && response.data[1] == oldCANBusID &&
                response.data[2] == 0x2 && response.data[3] == 0 &&
                response.can_id == newCANBusID) {
                    fprintf(stderr, "Reassignment successful.\n");
                    exit(0);
            } else if (response.data[3] != 0) {
                fprintf(stderr, "Reassignment failed with error %d\n", response.data[3]);
                exit(1);
            } else {
                // According to the docs, the response should come from the new ID, but.
                // for some reason, it does not.
                fprintf(stderr, "Reassignment may have failed (response from wrong ID).\n");
                exit(0);
            }
        } else {
            fprintf(stderr, "Reassignment failed (socket read).\n");
        }
    } else {
        fprintf(stderr, "Reassignment failed (socket write).\n");
        exit(1);
    }
}

/** Resets the center position of a single CANBus encoder to the current position. */
void resetCenterPositionOfCANBusEncoder(int sock, int CANBusID) {
    uint8_t data[8] = { 0x04, CANBusID, 0x0C, 0x01, 0, 0, 0, 0 };
    struct can_frame message = CANBusFrameMake(CANBusID, 4, data);

    fprintf(stderr, "Setting the midpint of the encoder to the current position.\n");
    if (write(sock, &message, sizeof(message)) == sizeof(message)) {
        struct can_frame response;
        if (read(sock, &response, sizeof(response)) == sizeof(response)) {
            if (response.data[0] == 0x4 && response.data[1] == CANBusID &&
                response.data[2] == 0xC && response.data[3] == 0) {
                    fprintf(stderr, "Reset successful.\n");
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

/** Resets the center position of the encoders to the current position (CANBus version). */
void resetCenterPositionsCANBus(int sock) {
  resetCenterPositionOfCANBusEncoder(sock, panCANBusID);
  resetCenterPositionOfCANBusEncoder(sock, tiltCANBusID);
}


#pragma mark - RS485/Modbus-specific encoder implementation

#else  // !USE_CANBUS

/**
 * Opens a serial device file descriptor for talking to serial/Modbus-based
 * position encoders.
 */
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

/** Updates the current encoder positions (serial/Modbus version). */
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

/** Resets the center position of the encoders to the current position (serial/Modbus version). */
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
#endif  // ENABLE_ENCODER_HARDWARE && ENABLE_HARDWARE


#pragma mark - Motor control thread

/**
 * The main loop of the motor control thread.
 *
 * This thread is responsible for taking the current pan and tilt speeds
 * (as set by other modules) and converting them into motor speed commands.
 */
void *runMotorControlThread(void *argIgnored) {
#if (ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE && ENABLE_HARDWARE)
  bool localDebug = false;
#endif

  while (1) {
    int scaledPanSpeed = g_pan_tilt_raw ?
        llabs(g_pan_speed) :
        llabs(scaleSpeed(g_pan_speed, SCALE_CORE, PAN_TILT_SCALE_HARDWARE,
                         motor_pan_scaled_data));
    int scaledTiltSpeed = g_pan_tilt_raw ?
        llabs(g_tilt_speed) :
        llabs(scaleSpeed(g_tilt_speed, SCALE_CORE, PAN_TILT_SCALE_HARDWARE,
                         motor_tilt_scaled_data));

#if (ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE)

    bool motorsAreSwapped = getConfigKeyBool(kMotorsAreSwappedKey);

    // Set the pan motor speed.
    if (localDebug) fprintf(stderr, "Setting motor A speed to %d.\n", scaledPanSpeed);
    Motor_Run(motorsAreSwapped ? MOTORB : MOTORA, g_pan_speed > 0 ? FORWARD : BACKWARD, scaledPanSpeed);

    // Set the tilt motor speed.
    if (localDebug) fprintf(stderr, "Setting motor B speed to %d.\n", scaledTiltSpeed);
    Motor_Run(motorsAreSwapped ? MOTORA : MOTORB, g_tilt_speed > 0 ? FORWARD : BACKWARD, scaledTiltSpeed);

    if (localDebug) fprintf(stderr, "Done.\n");

#else  // !(ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE)

    int pan_sign = panEncoderReversed() ? -1 : 1;
    int pan_sign_2 = (g_pan_speed < 0) ? -1 : 1;

    int tilt_sign = tiltEncoderReversed() ? -1 : 1;
    int tilt_sign_2 = (g_tilt_speed < 0) ? -1 : 1;

    /***************************************************************************
     * Fake hardware simulates encoder values based on the motor speed.  This  *
     * allows for some limited testing of recall functions without actual      *
     * hardware.                                                               *
     ***************************************************************************/
    g_last_pan_position += 6 * scaledPanSpeed * pan_sign * pan_sign_2 / 100;
    g_last_tilt_position += 6 * scaledTiltSpeed * tilt_sign * tilt_sign_2 / 100;

#endif  // ENABLE_HARDWARE && ENABLE_MOTOR_HARDWARE

#if ENABLE_STATUS_DEBUGGING || !ENABLE_HARDWARE
    // If hardware is disabled or if we have enabled status debugging, print
    // the current state of the motors (including zoom position and speed) here.

    int64_t zoom_speed = GET_ZOOM_SPEED();
    int64_t zoom_position = GET_ZOOM_POSITION();

#if ENABLE_HARDWARE
    static int count = 0;
    if (!(count++ % 50)) {
#endif  // ENABLE_HARDWARE
        printf("PAN SPEED: %" PRId64 " (%d) TILT SPEED: %" PRId64 " (%d) "
               "PAN POSITION: %" PRId64 " TILT POSITION: %" PRId64
               " ZOOM SPEED: %" PRId64 " ZOOM POSITION: %010" PRId64 "\n",
               g_pan_speed, scaledPanSpeed, g_tilt_speed, scaledTiltSpeed,
               g_last_pan_position, g_last_tilt_position, zoom_speed, zoom_position);
#if ENABLE_HARDWARE
    }
#endif  // ENABLE_HARDWARE
#endif  // ENABLE_STATUS_DEBUGGING || !ENABLE_HARDWARE

    usleep(10000);  // Update 100x per second (latency-critical).
  }
  return NULL;
}


#pragma mark - Calibration

// Public function.  Docs in header.
//
// Performs a calibration run on the pan and tilt hardware to obtain their actual speed
// (in encoder positions per second) at each speed (duty cycle).
void motorModuleCalibrate(void) {
  int64_t leftLimit = leftPanLimit();
  int64_t rightLimit = rightPanLimit();
  int64_t topLimit = topTiltLimit();
  int64_t bottomLimit = bottomTiltLimit();

  if (motor_enable_debugging) {
    fprintf(stderr, "LeftPanLimit: %" PRId64 "\n", leftLimit);
    fprintf(stderr, "RightPanLimit: %" PRId64 "\n", rightLimit);
    fprintf(stderr, "TopTiltLimit: %" PRId64 "\n", topLimit);
    fprintf(stderr, "BottomTiltLimit: %" PRId64 "\n", bottomLimit);
  }

  fprintf(stderr, "Calibrating pan and tilt motors.  This takes about 40 minutes.\n");

  int64_t *panCalibrationData = calibrationDataForMoveAlongAxis(
      axis_identifier_pan, leftLimit, rightLimit, 0, PAN_TILT_SCALE_HARDWARE, false);
  int64_t *tiltCalibrationData = calibrationDataForMoveAlongAxis(
      axis_identifier_tilt, topLimit, bottomLimit, 0, PAN_TILT_SCALE_HARDWARE, false);

  writeCalibrationDataForAxis(axis_identifier_pan, panCalibrationData, PAN_TILT_SCALE_HARDWARE);
  writeCalibrationDataForAxis(axis_identifier_tilt, tiltCalibrationData, PAN_TILT_SCALE_HARDWARE);

  fprintf(stderr, "Done calibrating motors.\n");
}

// Public function.  Docs in header.
//
// Returns the minimum nonzero number of positions per second that the pan axis moves
// at its slowest non-stalled speed.
int64_t motorMinimumPanPositionsPerSecond(void) {
  return minimumPositionsPerSecondForData(motor_pan_data, PAN_TILT_SCALE_HARDWARE);
}

// Public function.  Docs in header.
//
// Returns the minimum nonzero number of positions per second that the tilt axis moves
// at its slowest non-stalled speed.
int64_t motorMinimumTiltPositionsPerSecond(void) {
  return minimumPositionsPerSecondForData(motor_tilt_data, PAN_TILT_SCALE_HARDWARE);
}

// Public function.  Docs in header.
//
// Returns the number of positions per second that the pan axis moves at its fastest
// speed.
int64_t motorMaximumPanPositionsPerSecond(void) {
  return motor_pan_data[PAN_TILT_SCALE_HARDWARE];
}

// Public function.  Docs in header.
//
// Returns the number of positions per second that the tilt axis moves at its fastest
// speed.
int64_t motorMaximumTiltPositionsPerSecond(void) {
  return motor_tilt_data[PAN_TILT_SCALE_HARDWARE];
}

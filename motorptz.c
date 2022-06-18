#include "motorptz.h"

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

#if ENABLE_HARDWARE

#include "motorcontrol/DEV_Config.h"
#include "motorcontrol/Debug.h"
#include "motorcontrol/MotorDriver.h"
#include "motorcontrol/PCA9685.h"
#include "motorcontrol/DEV_Config.c"
#include "motorcontrol/MotorDriver.c"
#include "motorcontrol/PCA9685.c"

#else  // !ENABLE_HARDWARE
// For printing zoom speed.
#include "panasonicptz.h"
#endif  // ENABLE_HARDWARE


// Figure out these values when we have the hardware.  They will probably
// have to be configurable, too.
#define SERIAL_DEV_FILE_FOR_TILT "/dev/char/serial/uart0"
#define MIN_ENCODER_POSITION_FOR_TILT 3000
#define MAX_ENCODER_POSITION_FOR_TILT 20000
#define ENCODER_SCALE_FOR_TILT (MAX_ENCODER_POSITION_FOR_TILT - MIN_ENCODER_POSITION_FOR_TILT)
#define SERIAL_DEV_FILE_FOR_PAN "/dev/char/serial/uart1"
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

bool motorModuleInit(void) {

#if ENABLE_HARDWARE
    if (DEV_ModuleInit()) {
      return false;
    }
    Motor_Init();
#endif  // ENABLE_HARDWARE

  // Start the motor control thread in the background.
  if (motor_enable_debugging) fprintf(stderr, "Motor module init\n");
  pthread_create(&motor_control_thread, NULL, runMotorControlThread, NULL);
  pthread_create(&position_monitor_thread, NULL, runPositionMonitorThread, NULL);

  if (motor_enable_debugging) fprintf(stderr, "Motor module init done\n");
  return true;
}

bool motorSetIPAddress(char *address) {
  if (g_cameraIPAddr != NULL) {
    free(g_cameraIPAddr);
  }
  asprintf(&g_cameraIPAddr, "%s", address);
  return true;
}

bool motorSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed) {
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

void *runPositionMonitorThread(void *argIgnored) {
#if ENABLE_HARDWARE
  int tilt_fd = motorOpenSerialDev(SERIAL_DEV_FILE_FOR_TILT);
  int pan_fd = motorOpenSerialDev(SERIAL_DEV_FILE_FOR_PAN);

  if (pan_fd < 0 || tilt_fd < 0) {
    fprintf(stderr, "Could not open serial ports.  Disabling position monitoring.\n");
    return NULL;
  }
#endif  // ENABLE_HARDWARE

  // Read the position.
  while (1) {
#if ENABLE_HARDWARE
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

#else  // !ENABLE_HARDWARE
    g_last_pan_position += g_pan_speed;
    g_last_tilt_position += g_tilt_speed;
#endif  // ENABLE_HARDWARE
    usleep(10000);  // Update 100x per second (latency-critical).
  }
#if ENABLE_HARDWARE
  close(pan_fd);
  close(tilt_fd);
#endif  // ENABLE_HARDWARE
  return NULL;
}

#pragma mark - Motor control thread

void *runMotorControlThread(void *argIgnored) {
  while (1) {
#if ENABLE_HARDWARE
    Motor_Run(MOTORA, g_pan_speed > 0 ? FORWARD : BACKWARD, round(fabs(g_pan_speed * 100.0)));
    Motor_Run(MOTORB, g_tilt_speed > 0 ? FORWARD : BACKWARD, round(fabs(g_tilt_speed * 100.0)));
#else  // !ENABLE_HARDWARE
    int64_t zoom_speed = GET_ZOOM_SPEED();
    int64_t zoom_position = GET_ZOOM_POSITION();

    printf("PAN SPEED: %" PRId64 " TILT SPEED: %" PRId64 " PAN POSITION: %" PRId64 " TILT POSITION: %" PRId64
           " ZOOM SPEED: %" PRId64 " ZOOM POSITION: %" PRId64 "\n",
           g_pan_speed, g_tilt_speed, g_last_pan_position, g_last_tilt_position, zoom_speed, zoom_position);
#endif  // ENABLE_HARDWARE
    usleep(10000);  // Update 100x per second (latency-critical).
  }
  return NULL;
}

#include "panasonicptz.h"

#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>

#include <curl/curl.h>

#include "main.h"

#ifdef ENABLE_HARDWARE

#include "DEV_Config.h"
#include "Debug.h"
#include "MotorDriver.h"
#include "PCA9685.h"
#include "DEV_Config.c"
#include "MotorDriver.c"
#include "PCA9685.c"

#endif


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

#pragma mark URL support
 
typedef struct {
  char *data;
  size_t len;
} curl_buffer_t;

static bool pana_enable_debugging = false;

void freeURLBuffer(curl_buffer_t *buffer);
curl_buffer_t *fetchURLWithCURL(char *URL, CURL *handle);
static size_t writeMemoryCallback(void *contents, size_t chunkSize, size_t nChunks, void *userp);


#pragma mark - Panasonic implementation

pthread_t motor_control_thread;
pthread_t position_monitor_thread;
pthread_t tally_fetch_thread;
pthread_t zoom_position_thread;
void *runMotorControlThread(void *argIgnored);
void *runPositionMonitorThread(void *argIgnored);
void *runTallyThread(void *argIgnored);
void *runZoomPositionThread(void *argIgnored);

static CURL *tallyQueryHandle = NULL;
static CURL *zoomPositionQueryHandle = NULL;
static CURL *zoomSpeedSetHandle = NULL;

bool panaPanTiltPositionEnabled = false;
bool panaZoomPositionEnabled = false;

static char *g_cameraIPAddr = NULL;

static volatile double g_pan_speed = 0;
static volatile double g_tilt_speed = 0;

static volatile double g_last_pan_position = 0;
static volatile double g_last_tilt_position = 0;
static volatile double g_last_zoom_position = 0;
static volatile int g_last_tally_state = 0;

bool panaModuleInit(void) {

#ifdef ENABLE_HARDWARE
    if (DEV_ModuleInit()) {
      return false;
    }
    Motor_Init();
#endif

  // Start the motor control thread in the background.
  if (pana_enable_debugging) fprintf(stderr, "Module init\n");
  pthread_create(&motor_control_thread, NULL, runMotorControlThread, NULL);
  pthread_create(&position_monitor_thread, NULL, runPositionMonitorThread, NULL);
  pthread_create(&tally_fetch_thread, NULL, runTallyThread, NULL);
  pthread_create(&zoom_position_thread, NULL, runZoomPositionThread, NULL);

  curl_global_init(CURL_GLOBAL_ALL);
  tallyQueryHandle = curl_easy_init();
  curl_easy_setopt(tallyQueryHandle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
  curl_easy_setopt(tallyQueryHandle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  zoomPositionQueryHandle = curl_easy_init();
  curl_easy_setopt(zoomPositionQueryHandle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
  curl_easy_setopt(zoomPositionQueryHandle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  zoomSpeedSetHandle = curl_easy_init();
  curl_easy_setopt(zoomSpeedSetHandle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
  curl_easy_setopt(zoomSpeedSetHandle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  if (pana_enable_debugging) fprintf(stderr, "Module init done\n");
  return true;
}

bool panaSetIPAddress(char *address) {
  if (g_cameraIPAddr != NULL) {
    free(g_cameraIPAddr);
  }
  asprintf(&g_cameraIPAddr, "%s", address);
  return true;
}

bool panaSetPanSpeed(double speed) {
  g_pan_speed = speed;
  return true;
}

bool panaSetTiltSpeed(double speed) {
  g_tilt_speed = speed;
  return true;
}

int panaZoomIntSpeedForFloat(double floatSpeed) {
  // Range 01 - 99; 50 is stopped.
  double scaledValue = floatSpeed * 99.0;  // possible values.
  int valueWithoutClipping = (int)(scaledValue + 0.5);
  return (valueWithoutClipping < 1) ? 1 : (valueWithoutClipping > 99) ? 99 : valueWithoutClipping;
}

bool panaSetZoomSpeed(double speed) {
  int intSpeed = panaZoomIntSpeedForFloat(speed);
  bool localDebug = pana_enable_debugging || false;
  char *URL = NULL;
  asprintf(&URL, "http://%s/cgi-bin/aw_cam?cmd=%%23Z%02d&res=1", g_cameraIPAddr, intSpeed);  // #GZ
  curl_buffer_t *data = fetchURLWithCURL(URL, zoomSpeedSetHandle);

  // #Z50 stop
  if (data != NULL) {
    if (!strncmp(data->data, "zs", 2)) {
      int32_t value = atoi(&(data->data[2]));
      if (value != intSpeed) return false;
    } else {
      fprintf(stderr, "Unknown response for #GZ: %s", data->data);
      return false;
    }
    freeURLBuffer(data);
  }
  return true;
}

double panaGetPanPosition(void) {
  return g_last_pan_position;
}

double panaGetTiltPosition(void) {
  return g_last_tilt_position;
}

// Set zoom position absolute:
// Try #AXZxxx where xxx 0x555..0xfff (standard PTZ code from Panasonic PTZ cams)
// Try #GZxxx where xxx is 0x555..0xfff (adding a value to something supported by the AG-CX350)

double panaGetZoomPosition(void) {
  return g_last_zoom_position;
}

void updateZoomPosition(void) {
  bool localDebug = pana_enable_debugging || false;
  char *URL = NULL;
  asprintf(&URL, "http://%s/cgi-bin/aw_cam?cmd=%%23GZ&res=1", g_cameraIPAddr);  // #GZ
  curl_buffer_t *data = fetchURLWithCURL(URL, zoomPositionQueryHandle);

  if (data != NULL) {
    if (!strncmp(data->data, "gz", 2)) {
      char buf[6] = { '\0', '\0', '\0', '\0' };
      for (int i = 2; i < data->len && i < 5; i++) {
        if ((data->data[i] >= '0' && data->data[i] <= '9') ||
            (data->data[i] >= 'a' && data->data[i] <= 'f') ||
            (data->data[i] >= 'A' && data->data[i] <= 'F')) {
          buf[i - 2] = data->data[i];
        } else {
          break;
        }
      }
      int32_t value = strtol(buf, NULL, 16);
      // Convert from range 0x555..0xfff to -0.5..0.5
      g_last_zoom_position = ((value - 0x555) / 2730.0) -0.5;
      if (localDebug) fprintf(stderr, "Zoom position: %lf\n", g_last_zoom_position);
    } else {
      fprintf(stderr, "Unknown response for #GZ: %s", data->data);
    }
    freeURLBuffer(data);
  }
}

int panaGetTallyState(void) {
  return g_last_tally_state;
}

// Eventually, this MAY be implemented differently with a direct call to the camera.
bool panaSetZoomPosition(double position, double maxSpeed) {
    return setZoomPositionIncrementally(position, maxSpeed);
}

// If farther than one-sixth the zoom range, go at full speed.  Otherwise, go at a
// speed proportional to the distance divided by one-sixth the range.
double panaPanSpeed(double fromPosition, double toPosition) {
  double distance = toPosition - fromPosition;
  if (fabs(distance) > 0.3) return 1.0;
  return distance / 0.3;
}

double panaTiltSpeed(double fromPosition, double toPosition) {
  return 1.0;
}

double panaZoomSpeed(double fromPosition, double toPosition) {
  return 1.0;
}


#pragma mark - Position monitor thread

int panaOpenSerialDev(char *path) {
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
  int tilt_fd = panaOpenSerialDev(SERIAL_DEV_FILE_FOR_TILT);
  int pan_fd = panaOpenSerialDev(SERIAL_DEV_FILE_FOR_PAN);

  if (pan_fd < 0 || tilt_fd < 0) {
    fprintf(stderr, "Could not open serial ports.  Disabling position monitoring.\n");
    return NULL;
  }

  // Read the position.
  while (1) {
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

    double g_last_pan_position =
        (((pan_position - MIN_ENCODER_POSITION_FOR_PAN) / (double)ENCODER_SCALE_FOR_PAN) - 0.5) * 2;

    double g_last_tilt_position =
        (((tilt_position - MIN_ENCODER_POSITION_FOR_TILT) / (double)ENCODER_SCALE_FOR_TILT) - 0.5) * 2;

    usleep(10000);  // Update 100x per second (latency-critical).
  }
  close(pan_fd);
  close(tilt_fd);
  return NULL;
}

#pragma mark - Tally monitor thread

void *runTallyThread(void *argIgnored) {
  bool localDebug = pana_enable_debugging || false;
  if (localDebug) fprintf(stderr, "Starting tally thread.\n");
  char *redURL = NULL;
  char *greenURL = NULL;
  asprintf(&redURL, "http://%s/cgi-bin/aw_cam?cmd=QLR&res=1", g_cameraIPAddr);
  asprintf(&greenURL, "http://%s/cgi-bin/aw_cam?cmd=QLG&res=1", g_cameraIPAddr);
  while (1) {
    bool redState = false;
    bool greenState = false;
    if (localDebug) fprintf(stderr, "Fetching RED\n");
    curl_buffer_t *data = fetchURLWithCURL(redURL, tallyQueryHandle);
    if (data != NULL) {
      if (!strncmp(data->data, "TLR:", 4)) {
        redState = data->data[4] == '1';
      } else {
        fprintf(stderr, "Unknown response for QLR: %s", data->data);
      }
      freeURLBuffer(data);
    }
    if (localDebug) fprintf(stderr, "Fetching GREEN\n");
    data = fetchURLWithCURL(greenURL, tallyQueryHandle);
    if (data != NULL) {
      if (!strncmp(data->data, "TLG:", 4)) {
        greenState = data->data[4] == '1';
      } else {
        fprintf(stderr, "Unknown response for QLG: %s", data->data);
      }
      freeURLBuffer(data);
    }
    g_last_tally_state = redState ? 5 : greenState ?  6 : 0;
    if (localDebug) fprintf(stderr, "State: %d\n", g_last_tally_state);
    usleep(100000);  // Update 10x per second (non-latency-critical).
  }
  return NULL;
}


#pragma mark - Motor control thread

void *runMotorControlThread(void *argIgnored) {
  while (1) {
#ifdef ENABLE_HARDWARE
    Motor_Run(MOTORA, g_pan_speed > 0 ? FORWARD : BACKWARD, round(fabs(g_pan_speed * 100.0)));
    Motor_Run(MOTORB, g_tilt_speed > 0 ? FORWARD : BACKWARD, round(fabs(g_tilt_speed * 100.0)));
#else
    printf("ZOOM SPEED: %lf\nTILT SPEED: %lf\n", g_pan_speed, g_tilt_speed);
#endif
    usleep(10000);  // Update 100x per second (latency-critical).
  }
  return NULL;
}


void *runZoomPositionThread(void *argIgnored) {
  if (pana_enable_debugging) fprintf(stderr, "Starting zoom position thread.\n");
  while (1) {
    updateZoomPosition();
    usleep(10000);  // Update 100x per second (latency-critical).
  }
}


#pragma mark - URL support

curl_buffer_t *fetchURLWithCURL(char *URL, CURL *handle) {
  curl_buffer_t *chunk = malloc(sizeof(curl_buffer_t));;
  chunk->data = malloc(1);
  chunk->len = 0;

  curl_easy_setopt(handle, CURLOPT_URL, URL);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void *)chunk);
 
  CURLcode res = curl_easy_perform(handle);
 
  if(res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
    free(chunk->data);
    free(chunk);
    return NULL;
  }

  return chunk;
}

void freeURLBuffer(curl_buffer_t *buffer) {
  free(buffer->data);
  free(buffer);
}

// Realistically, we won't ever get the chance to call this.
void cleanupCURLBits(void) {
  curl_easy_cleanup(tallyQueryHandle);
  curl_easy_cleanup(zoomPositionQueryHandle);
  curl_global_cleanup();
}

static size_t writeMemoryCallback(void *contents, size_t chunkSize, size_t nChunks, void *userp)
{
  size_t totalSize = chunkSize * nChunks;
  curl_buffer_t *chunk = (curl_buffer_t *)userp;
 
  char *ptr = realloc(chunk->data, chunk->len + totalSize + 1);
  if(!ptr) {
    fprintf(stderr, "Out of memory in realloc\n");
    return 0;
  }
 
  chunk->data = ptr;
  bcopy(contents, &(chunk->data[chunk->len]), totalSize);
  chunk->len += totalSize;
  chunk->data[chunk->len] = 0;
 
  return totalSize;
}

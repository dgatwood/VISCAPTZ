#include "panasonicptz.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>

#include <curl/curl.h>

#include "main.h"

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
pthread_t tally_fetch_thread;
pthread_t zoom_position_thread;
void *runMotorControlThread(void *argIgnored);
void *runTallyThread(void *argIgnored);
void *runZoomPositionThread(void *argIgnored);

static CURL *tallyQueryHandle = NULL;
static CURL *zoomPositionQueryHandle = NULL;

bool panaPanTiltPositionEnabled = false;
bool panaZoomPositionEnabled = false;

static char *g_cameraIPAddr = NULL;

static volatile double g_pan_speed = 0;
static volatile double g_tilt_speed = 0;
static volatile double g_zoom_speed = 0;

static volatile double g_last_pan_position = 0;
static volatile double g_last_tilt_position = 0;
static volatile double g_last_zoom_position = 0;
static volatile int g_last_tally_state = 0;

bool panaModuleInit(void) {
  // Start the motor control thread in the background.
  if (pana_enable_debugging) fprintf(stderr, "Module init\n");
  pthread_create(&motor_control_thread, NULL, runMotorControlThread, NULL);
  pthread_create(&tally_fetch_thread, NULL, runTallyThread, NULL);
  pthread_create(&zoom_position_thread, NULL, runZoomPositionThread, NULL);

  curl_global_init(CURL_GLOBAL_ALL);
  tallyQueryHandle = curl_easy_init();
  curl_easy_setopt(tallyQueryHandle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
  curl_easy_setopt(tallyQueryHandle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  zoomPositionQueryHandle = curl_easy_init();
  curl_easy_setopt(zoomPositionQueryHandle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
  curl_easy_setopt(zoomPositionQueryHandle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

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

bool panaSetZoomSpeed(double speed) {
  g_zoom_speed = speed;
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

#pragma mark - Position monitor thread


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
  while (1);  // Just spin for now.
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

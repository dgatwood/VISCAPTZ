#include "panasonicptz.h"

#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <termios.h>
#include <unistd.h>

#include <curl/curl.h>

#include "main.h"
#include "constants.h"

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

#define FREEMULTI(array) freeMulti(array, sizeof(array) / sizeof(array[0]))

#pragma mark URL support
 
typedef struct {
  char *data;
  size_t len;
} curl_buffer_t;

static bool pana_enable_debugging = false;

char *panaIntString(uint64_t value, int digits, bool hex);
void freeURLBuffer(curl_buffer_t *buffer);
curl_buffer_t *fetchURLWithCURL(char *URL, CURL *handle);
static size_t writeMemoryCallback(void *contents, size_t chunkSize, size_t nChunks, void *userp);
char *sendCommand(const char *group, const char *command, char *values[],
                  int numValues, const char *responsePrefix);

#pragma mark - Panasonic implementation

static CURL *tallyQueryHandle = NULL;
static CURL *zoomPositionQueryHandle = NULL;
static CURL *zoomSpeedSetHandle = NULL;

static char *g_cameraIPAddr = NULL;

bool panaModuleInit(void) {
    // fprintf(stderr, "0x444(2) : %s\n", panaIntString(0x444, 2, true));
    // fprintf(stderr, "0x444(3) : %s\n", panaIntString(0x444, 3, true));
    // fprintf(stderr, "0x444(4) : %s\n", panaIntString(0x444, 4, true));
    // fprintf(stderr, "0x444(5) : %s\n", panaIntString(0x444, 5, true));

    // fprintf(stderr, "333(2) : %s\n", panaIntString(333, 2, false));
    // fprintf(stderr, "333(3) : %s\n", panaIntString(333, 3, false));
    // fprintf(stderr, "333(4) : %s\n", panaIntString(333, 4, false));
    // fprintf(stderr, "333(5) : %s\n", panaIntString(333, 5, false));

#if USE_PANASONIC_PTZ
    if (pana_enable_debugging) fprintf(stderr, "Panasonic module init\n");

    curl_global_init(CURL_GLOBAL_ALL);

    if (pana_enable_debugging) fprintf(stderr, "Panasonic module init done\n");
#else
    if (pana_enable_debugging) fprintf(stderr, "Panasonic module init skipped\n");
#endif
    return true;
}

bool panaModuleTeardown(void) {
    curl_global_cleanup();
    return true;
}

bool panaSetIPAddress(char *address) {
  if (g_cameraIPAddr != NULL) {
    free(g_cameraIPAddr);
  }
  asprintf(&g_cameraIPAddr, "%s", address);
  return true;
}

int hexDigits(uint64_t value) {
  int digits = 1;
  value = value >> 4;
  while (value > 0) {
    value = value >> 4;
    digits++;
  }
  return digits;
}

int decDigits(uint64_t value) {
  int digits = 1;
  value = value / 10;
  while (value > 0) {
    value = value / 10;
    digits++;
  }
  return digits;
}

char *panaIntString(uint64_t value, int digits, bool hex) {
    int actualDigits = hex ? hexDigits(value) : decDigits(value);
    ssize_t length = MAX(digits, actualDigits);
    char *str = malloc(length + 1);
    memset(str, (int)'0', length);
    if (hex) {
        sprintf(str + MAX(0, (digits - actualDigits)), "%" PRIx64 "", value);
    } else {
        sprintf(str + MAX(0, (digits - actualDigits)), "%" PRId64 "", value);
    }
    return str;
}

void freeMulti(char **array, ssize_t count) {
  for (ssize_t i = 0; i < count; i++) {
    free(array[i]);
  }
}

#if !PANASONIC_PTZ_ZOOM_ONLY
bool panaSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed) {
    int scaledTiltSpeed = scaleSpeed(tiltSpeed, SCALE_CORE, PAN_TILT_SCALE_HARDWARE);

    bool localDebug = pana_enable_debugging || false;
    static int64_t last_zoom_position = 0;
    char *values[2] = { panaIntString(panSpeed, 3, true), panaIntString(scaledTiltSpeed, 3, true) };
    char *response = sendCommand("ptz", "#PTS", values, 2, "gz");
    bool retval = false;
    if (response != NULL) {
        retval = true;
        free(response);
    }

    FREEMULTI(values);
    return retval;
}

bool panaSetPanTiltPosition(int64_t panPosition, int64_t panSpeed,
                            int64_t tiltPosition, int64_t tiltSpeed) {
    // Panasonic's documentation makes no sense, so this is probably wrong,
    // and I don't have the hardware required to try it, so this should be
    // considered entirely unsupported.  Why don't these use PAN_TILT_SCALE_HARDWARE?
    int convertedPanSpeed = scaleSpeed(panSpeed, SCALE_CORE, 0x1D) - 1;    // Speeds are 0 through 0x1D?
    int convertedTiltSpeed = scaleSpeed(tiltSpeed, SCALE_CORE, 3) - 1;  // Speeds are 0, 1, or 2?

    char *values[4] = {
        panaIntString(panPosition, 4, true),
        panaIntString(tiltPosition, 4, true),
        panaIntString(convertedPanSpeed, 2, true),
        panaIntString(convertedTiltSpeed, 1, true)
    };
    char *response = sendCommand("ptz", "#APS", values, 4, "aPS");
    if (response != NULL) {
        free(response);
        FREEMULTI(values);
        return true;
    }
    FREEMULTI(values);
    return false;
}
#endif

bool panaSetZoomPosition(int64_t position, int64_t maxSpeed) {
    char *value = panaIntString(position, 3, true);
    char *response = sendCommand("ptz", "#AXZ", &value, 1, "axz");
    if (response != NULL) {
        free(response);
        free(value);
        return true;
    }
    free(value);
    return false;
}

// Sends the command, strips the specified prefix from the response, and returns it as a
// newly allocated string.
char *sendCommand(const char *group, const char *command, char *values[],
                  int numValues, const char *responsePrefix) {
    CURL *curlQueryHandle = curl_easy_init();
    curl_easy_setopt(curlQueryHandle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
    curl_easy_setopt(curlQueryHandle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    char *encoded_command = curl_easy_escape(curlQueryHandle, command, 0);
    // fprintf(stderr, "Command: \"%s\" encoded command: \"%s\"\n", command, encoded_command);

    bool localDebug = pana_enable_debugging || false;
    char *URL = NULL;
    char *valueString = NULL;
    asprintf(&valueString, "");
    for (int i = 0 ; i < numValues; i++) {
        char *temp = valueString;
        asprintf(&valueString, "%s%s", temp, values[i]);
        free(temp);
    }

    asprintf(&URL, "http://%s/cgi-bin/aw_%s?cmd=%s%s&res=1",
             g_cameraIPAddr, group, encoded_command, valueString);
    curl_buffer_t *data = fetchURLWithCURL(URL, curlQueryHandle);

    char *retval = NULL;
    if (data != NULL) {
      ssize_t prefixLength = strlen(responsePrefix);
      if (!strncmp(data->data, responsePrefix, prefixLength)) {
        asprintf(&retval, "%s", &(data->data[prefixLength]));
      } else {
        fprintf(stderr, "Unexpected prefix %s for %s (expected %s)\n",
                data->data, command, responsePrefix);
      }
      freeURLBuffer(data);
    }

    free(encoded_command);
    free(valueString);

    curl_easy_cleanup(curlQueryHandle);

    return retval;
}

static int64_t last_zoom_speed = 0;

int64_t panaGetZoomSpeed(void) {
    // Just return the last cached value.  The camera doesn't provide
    // a way to query this.
    return last_zoom_speed;
}

bool panaSetZoomSpeed(int64_t speed) {
    bool localDebug = false;
    last_zoom_speed = speed;

    int intSpeed = scaleSpeed(speed, SCALE_CORE, ZOOM_SCALE_HARDWARE) + 50;
    char *intSpeedString = panaIntString(intSpeed, 2, false);

    if (localDebug) {
        fprintf(stderr, "Core speed %" PRId64 "\n", speed);
        fprintf(stderr, "Speed %d\n", intSpeed);
        fprintf(stderr, "Speed string %s\n", intSpeedString);
    }

    bool retval = true;
    char *response = sendCommand("ptz", "#Z", &intSpeedString, 1, "zS");
    if (!response || atoi(response) != intSpeed) {
        retval = false;
        free(response);
    }
    free(intSpeedString);
    return retval;
}

bool panaGetPanTiltPosition(int64_t *panPosition, int64_t *tiltPosition) {
    bool localDebug = pana_enable_debugging || false;
    static int64_t last_pan_position = 0;
    static int64_t last_tilt_position = 0;
    char *response = sendCommand("ptz", "#APC", NULL, 0, "aPC");
    bool retval = false;
    if (response != NULL) {
        retval = true;
        int32_t value = strtol(response, NULL, 16);
        last_pan_position = (value >> 12) & 0xfff;
        last_tilt_position = value & 0xfff;

        if (localDebug) fprintf(stderr, "Pan position: %" PRId64 "\n", last_pan_position);
        if (localDebug) fprintf(stderr, "Tilt position: %" PRId64 "\n", last_tilt_position);
        free(response);
    }
    return retval;
}

int64_t panaGetZoomPosition(void) {
    bool localDebug = pana_enable_debugging || false;
    static int64_t last_zoom_position = 0;
    char *response = sendCommand("ptz", "#GZ", NULL, 0, "gz");
    if (response != NULL && strlen(response)) {
        int32_t value = strtol(response, NULL, 16);
        last_zoom_position = value;
        if (localDebug) fprintf(stderr, "Zoom position: %" PRId64 "\n", last_zoom_position);
        free(response);
    }
    return last_zoom_position;
}

int panaGetTallyState(void) {
    bool localDebug = pana_enable_debugging || false;
    static int g_last_tally_state = 0;
    bool redState = false;
    bool greenState = false;
    if (localDebug) fprintf(stderr, "Fetching RED\n");

    char *redResponse = sendCommand("cam", "QLR", NULL, 0, "TLR:");
    char *greenResponse = sendCommand("cam", "QLG", NULL, 0, "TLG:");

    if (redResponse != NULL && greenResponse != NULL) {
        redState = redResponse[0] == '1';

        greenState = greenResponse[0] == '1';

        g_last_tally_state = redState ? kTallyStateRed : greenState ? kTallyStateGreen : 0;
        if (localDebug) {
            fprintf(stderr, "Tally state: %d\n", g_last_tally_state);
        }
    } else if (localDebug) {
        fprintf(stderr, "Request failed.  Returning previons tally state: %d\n", g_last_tally_state);
    }
    if (redResponse != NULL) {
        free(redResponse);
    }
    if (greenResponse != NULL) {
        free(greenResponse);
    }
    return g_last_tally_state;
}

bool panaSetTallyState(int tallyState) {
    bool localDebug = pana_enable_debugging || false;
    int redState = (tallyState == 5);
    int greenState = (tallyState == 6);
    if (localDebug) fprintf(stderr, "Fetching RED\n");

    char *redTallyState = panaIntString(redState, 1, false);
    char *greenTallyState = panaIntString(greenState, 1, false);

    char *redResponse = sendCommand("cam", "TLR:", &redTallyState, 1, "TLR:");
    char *greenResponse = sendCommand("cam", "TLG:", &greenTallyState, 1, "TLG:");

    bool retval = (redResponse != NULL && greenResponse != NULL);

    if (redResponse != NULL) {
        free(redResponse);
    }
    if (greenResponse != NULL) {
        free(greenResponse);
    }
    free(redTallyState);
    free(greenTallyState);

    return retval;
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

#include "panasonicptz.h"

#include <assert.h>
#include <curl/curl.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <termios.h>
#include <unistd.h>

#include "main.h"
#include "configurator.h"
#include "constants.h"
#include "panasonic_shared.h"

#if USE_PANASONIC_PTZ && !ENABLE_P2_MODE  // Otherwise, this .c file is a no-op.

#pragma mark - Data types

/** A sized buffer for passing around data from libcurl. */
typedef struct {
  char *data;
  size_t len;
} curl_buffer_t;


#pragma mark - Function prototypes

#define FREEMULTI(array) freeMulti(array, sizeof(array) / sizeof(array[0]))
void freeMulti(char **array, ssize_t count);

char *panaIntString(uint64_t value, int digits, bool hex);

void freeURLBuffer(curl_buffer_t *buffer);
curl_buffer_t *fetchURLWithCURL(char *URL, CURL *handle);
static size_t writeMemoryCallback(void *contents, size_t chunkSize, size_t nChunks, void *userp);

char *sendCommand(const char *group, const char *command, char *values[],
                  int numValues, const char *responsePrefix);

void runPanasonicTests(void);


#pragma mark - Global variables

/** The last zoom speed that was set. */
static int64_t gLastZoomSpeed = 0;

/** The IP address of the camera. */
static char *g_cameraIPAddr = NULL;

#if USE_PANASONIC_PTZ
  /** The zoom calibration data in core-scaled form (1000 * value / max_value). */
  int32_t *panasonicScaledZoomCalibrationData = NULL;

  #if !PANASONIC_PTZ_ZOOM_ONLY
    /** The pan calibration data in raw form (positions per second for each speed). */
    int64_t *pana_pan_data = NULL;

    /** The pan calibration data in core-scaled form (1000 * value / max_value). */
    int32_t *pana_pan_scaled_data = NULL;

    /** The pan calibration data in raw form (positions per second for each speed). */
    int64_t *pana_tilt_data = NULL;

    /** The tilt calibration data in core-scaled form (1000 * value / max_value). */
    int32_t *pana_tilt_scaled_data = NULL;

  #endif  // !PANASONIC_PTZ_ZOOM_ONLY
#endif  // USE_PANASONIC_PTZ


#pragma mark - Panasonic module implementation

// Public function.  Docs in header.
//
// Initializes the module.
bool panaModuleInit(void) {
  runPanasonicTests();

  #if USE_PANASONIC_PTZ
    if (pana_enable_debugging) fprintf(stderr, "Panasonic module init\n");

    curl_global_init(CURL_GLOBAL_ALL);

    if (pana_enable_debugging) fprintf(stderr, "Panasonic module init done\n");
  #else
    if (pana_enable_debugging) fprintf(stderr, "Panasonic module init skipped\n");
  #endif

  populateZoomNonlinearityTable();
  return panaModuleReload();
}

// Public function.  Docs in header.
//
// Starts the module's threads.  (Nothing to do for this module.)
bool panaModuleStart(void) {
  return true;
}

// Public function.  Docs in header.
//
// Reinitializes the module after calibration.
bool panaModuleReload(void) {
  #if USE_PANASONIC_PTZ
    int maxSpeed = 0;
    bool localDebug = false;
    panasonicSharedInit(&panasonicZoomCalibrationData, &maxSpeed);
    if (maxSpeed == ZOOM_SCALE_HARDWARE) {
        panasonicScaledZoomCalibrationData =
            convertSpeedValues(panasonicZoomCalibrationData, ZOOM_SCALE_HARDWARE,
                               axis_identifier_zoom);
        if (localDebug) {
          for (int i=0; i<=ZOOM_SCALE_HARDWARE; i++) {
            fprintf(stderr, "%d: raw: %" PRId64 "\n", i, panasonicZoomCalibrationData[i]);
            fprintf(stderr, "%d: scaled: %d\n", i, panasonicScaledZoomCalibrationData[i]);
          }
        }
    } else {
        fprintf(stderr, "Ignoring calibration data because the scale (%d) is incorrect.\n",
                maxSpeed);
    }

    #if !PANASONIC_PTZ_ZOOM_ONLY
      pana_pan_data =
          readCalibrationDataForAxis(axis_identifier_pan, &maxSpeed);
      if (maxSpeed == PAN_TILT_SCALE_HARDWARE) {
          pana_pan_scaled_data =
              convertSpeedValues(pana_pan_data, PAN_TILT_SCALE_HARDWARE,
                               axis_identifier_pan);
      }

      pana_tilt_data =
          readCalibrationDataForAxis(axis_identifier_tilt, &maxSpeed);
      if (maxSpeed == PAN_TILT_SCALE_HARDWARE) {
          pana_tilt_scaled_data =
              convertSpeedValues(pana_tilt_data, PAN_TILT_SCALE_HARDWARE,
                               axis_identifier_tilt);
      }
    #endif  // !PANASONIC_PTZ_ZOOM_ONLY
  #endif  // USE_PANASONIC_PTZ
  return true;
}

// Public function.  Docs in header.
//
// Tears down the module.
bool panaModuleTeardown(void) {
    curl_global_cleanup();
    return true;
}

// Public function.  Docs in header.
//
// Sets the camera's IP address.
bool panaSetIPAddress(char *address) {
  if (g_cameraIPAddr != NULL) {
    free(g_cameraIPAddr);
  }
  asprintf(&g_cameraIPAddr, "%s", address);
  return true;
}

/** Returns the number of characters required to represent the specified value in hexadecimal. */
int hexDigits(uint64_t value) {
  int digits = 1;
  value = value >> 4;
  while (value > 0) {
    value = value >> 4;
    digits++;
  }
  return digits;
}

/** Returns the number of characters required to represent the specified value in base 10. */
int decDigits(uint64_t value) {
  int digits = 1;
  value = value / 10;
  while (value > 0) {
    value = value / 10;
    digits++;
  }
  return digits;
}

/**
 * Returns a string containing the provided number, zero-padded to have the specified
 * number of digits.  If hex is true, the result is in base 16 (but with no leading 0x).
 * Otherwise, the result is in base 10.
 */
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

/**
 * Frees each string in an array of the specified length.
 */
void freeMulti(char **array, ssize_t count) {
  for (ssize_t i = 0; i < count; i++) {
    free(array[i]);
  }
}

#if !PANASONIC_PTZ_ZOOM_ONLY

// Public function.  Docs in header.
//
// Sets the pan and tilt speed in core scale or hardware scale (if isRaw is true).
bool panaSetPanTiltSpeed(int64_t panSpeed, int64_t tiltSpeed, bool isRaw) {
    int scaledPanSpeed =
        isRaw ? panSpeed : scaleSpeed(panSpeed, SCALE_CORE, PAN_TILT_SCALE_HARDWARE,
                                       pana_pan_scaled_data);
    int scaledTiltSpeed =
        isRaw ? tiltSpeed : scaleSpeed(tiltSpeed, SCALE_CORE, PAN_TILT_SCALE_HARDWARE,
                                       pana_tilt_scaled_data);

    bool localDebug = pana_enable_debugging || false;
    static int64_t last_zoom_position = 0;
    char *values[2] = { panaIntString(scaledPanSpeed, 3, true), panaIntString(scaledTiltSpeed, 3, true) };
    char *response = sendCommand("ptz", "#PTS", values, 2, "gz");
    bool retval = false;
    if (response != NULL) {
        retval = true;
        free(response);
    }

    FREEMULTI(values);
    return retval;
}

// Public function.  Docs in header.
//
// Moves the camera to the specified pan and tilt position.
bool panaSetPanTiltPosition(int64_t panPosition, int64_t panSpeed,
                            int64_t tiltPosition, int64_t tiltSpeed) {
    // Panasonic's documentation makes no sense, so this is probably wrong,
    // and I don't have the hardware required to try it, so this should be
    // considered entirely unsupported.  Why don't these use PAN_TILT_SCALE_HARDWARE?
    int convertedPanSpeed = scaleSpeed(panSpeed, SCALE_CORE, 0x1D, NULL) - 1;    // Speeds are 0 through 0x1D?
    int convertedTiltSpeed = scaleSpeed(tiltSpeed, SCALE_CORE, 3, NULL) - 1;  // Speeds are 0, 1, or 2?

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

// Public function.  Docs in header.
//
// Returns true and populates the current minimum and maximum zoom position values, or
// returns false if there is no calibration data.
bool panaGetZoomRange(int64_t *min, int64_t *max) {
  // Don't trust the range if there's no calibration data.
  if (panasonicScaledZoomCalibrationData == NULL) {
    return false;
  }
  if (min != NULL) {
    *min = zoomOutLimit();
  }

  if (max != NULL) {
    *max = zoomInLimit();
  }
  return true;
}

// Public function.  Docs in header.
//
// Tells the camera to move to the specified zoom position.  Not used because we don't have
// any real control over speed versus time that way.
bool panaSetZoomPosition(int64_t position, int64_t maxSpeed) {

    fprintf(stderr, "SET ZOOM POSITION TO %" PRId64 " SPEED %d\n", position, (int)maxSpeed);

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

/**
 * Sends the specified command to a Panasonic camera.
 *
 * @param group          The command group (CGI script name), typically "ptz" or "cam".
 * @param command        The command code.
 * @param values         An array of string values to concatenate onto the command.
 * @param numValues      The size of the additional values array.
 * @param responsePrefix The expected prefix for the response.
 *
 * @result
 *     Returns the response with the response prefix stripped from the beginning, and
 *     returns it as a newly allocated string.  This code prints a warning if the
 *     expected prefix is not present.
 */
char *sendCommand(const char *group, const char *command, char *values[],
                  int numValues, const char *responsePrefix) {
    CURL *curlQueryHandle = curl_easy_init();

    // For thread safety.
    curl_easy_setopt(curlQueryHandle, CURLOPT_NOSIGNAL, 1L);

    // The following line deserves explanation.  I originally used a 2-second timeout, but
    // then wondered why (when it turned out the camera's IP was wrong) this software failed to
    // respond to control commands, then suddenly started moving uncontrollably minutes later.
    // It turned out that received packets were queueing up in the kernel, and being basically
    // delivered one per second because the tally light operations currently happen on the main
    // network thread.  This should probably be moved to a separate thread, but a short timeout
    // is still preferable, because you don't want the camera to keep zooming forever if a
    // request stalls for any reason.
    curl_easy_setopt(curlQueryHandle, CURLOPT_TIMEOUT_MS, 300);  // By IP, so very short limit.
    curl_easy_setopt(curlQueryHandle, CURLOPT_WRITEFUNCTION, writeMemoryCallback);
    curl_easy_setopt(curlQueryHandle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    char *encoded_command = curl_easy_escape(curlQueryHandle, command, 0);
    bool localDebug = pana_enable_debugging || false;

    if (localDebug) {
        fprintf(stderr, "Command: \"%s\" encoded command: \"%s\"\n", command, encoded_command);
    }

    char *URL = NULL;
    char *valueString = NULL;
    asprintf(&valueString, "%s", "");
    for (int i = 0 ; i < numValues; i++) {
        char *temp = valueString;
        asprintf(&valueString, "%s%s", temp, values[i]);
        free(temp);
    }

    asprintf(&URL, "http://%s/cgi-bin/aw_%s?cmd=%s%s&res=1",
             g_cameraIPAddr, group, encoded_command, valueString);

    if (localDebug || false) {
        fprintf(stderr, "Fetching URL: %s\n", URL);
    }

    curl_buffer_t *data = fetchURLWithCURL(URL, curlQueryHandle);

    if (localDebug || false) {
        fprintf(stderr, "URL fetch raw return is \"%s\"\n", data ? data->data : NULL);
    }

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
    } else if (localDebug) {
        fprintf(stderr, "URL fetch returned NULL\n");
    }



    free(encoded_command);
    free(valueString);

    curl_easy_cleanup(curlQueryHandle);

    return retval;
}

// Public function.  Docs in header.
//
// Returns the camera's most recent zoom speed (for debugging only).
int64_t panaGetZoomSpeed(void) {
    // Just return the last cached value.  The camera doesn't provide
    // a way to query this.
    return gLastZoomSpeed;
}

// Public function.  Docs in header.
//
// Sets the camera's zoom speed.
int64_t panaGetZoomPositionRaw(void);
bool panaSetZoomSpeed(int64_t speed, bool isRaw) {
    bool localDebug = false || pana_enable_debugging;
    gLastZoomSpeed = speed;

    int intSpeed =
        isRaw ? (speed + 50) : scaleSpeed(speed, SCALE_CORE, ZOOM_SCALE_HARDWARE,
                                          panasonicScaledZoomCalibrationData) + 50;
    char *intSpeedString = panaIntString(intSpeed, 2, false);

    if (localDebug) {
        fprintf(stderr, "Zoom Core speed %" PRId64 "\n", speed);
        fprintf(stderr, "Zoom Speed %d\n", intSpeed - 50);
        fprintf(stderr, "Zoom Speed string %s\n", intSpeedString);
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

// Public function.  Docs in header.
//
// Gets the camera's current pan and tilt position.
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

// Similar to panaGetZoomPosition, but does not correct for the nonlinearity of
// the position data.  [redacted swearing at Panasonic]
int64_t panaGetZoomPositionRaw(void) {
    bool localDebug = pana_enable_debugging || false;
    static int64_t last_zoom_position = 0;
    char *response = sendCommand("ptz", "#GZ", NULL, 0, "gz");
    if (response != NULL && strlen(response)) {
        int32_t value = strtol(response, NULL, 16);
        last_zoom_position = value;
        if (localDebug) fprintf(stderr, "Zoom position: %" PRId64 "\n", last_zoom_position);
        free(response);
    } else {
        if (localDebug) fprintf(stderr, "Did not get response from CGI.  Returning last value.\n");
    }
    return last_zoom_position;
}

// Public function.  Docs in header.
//
// Gets the camera's current zoom position.
int64_t panaGetZoomPosition(void) {
    bool localDebug = false;
    int64_t nonlinearZoomPosition = panaGetZoomPositionRaw();
    int64_t retval = panaMakeZoomLinear(nonlinearZoomPosition);
    if (localDebug) {
        fprintf(stderr, "ZOOMPOS: %" PRId64 "\n", retval);
    }
    return retval;
}

// Public function.  Docs in header.
//
// Returns the camera's current tally light state.
int panaGetTallyState(void) {
    bool localDebug = pana_enable_debugging || false;
    static int g_last_tally_state = 0;
    bool redState = false;
    bool greenState = false;
    if (localDebug) fprintf(stderr, "Fetching RED\n");

    char *redResponse = sendCommand("cam", "QLR", NULL, 0, "OLR:");
    char *greenResponse = sendCommand("cam", "QLG", NULL, 0, "OLG:");

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

// Public function.  Docs in header.
//
// Sets the camera's tally light state.
bool panaSetTallyState(int tallyState) {

    if (pana_enable_debugging) {
        fprintf(stderr, "@@@ UPDATING PANASONIC TALLY STATE NOW %s\n",
                tallyStateName(tallyState));
    }

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

/** Fetches the provided URL with the provided handle and returns the data in a buffer. */
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

/** Frees a curl_buffer_t, including the data inside it. */
void freeURLBuffer(curl_buffer_t *buffer) {
  free(buffer->data);
  free(buffer);
}

/** Curl callback that accumulates data in an in-memory buffer. */
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


#pragma mark - Tests

/** Runs some basic tests of miscellaneous routines. */
void runPanasonicTests(void) {
  fprintf(stderr, "Running Panasonic module tests.\n");

  char *tempValue = panaIntString(0x444, 2, true);
  assert(!strcmp(tempValue, "444"));
  free(tempValue);

  tempValue = panaIntString(0x444, 3, true);
  assert(!strcmp(tempValue, "444"));
  free(tempValue);

  tempValue = panaIntString(0x444, 3, true);
  assert(!strcmp(tempValue, "444"));
  free(tempValue);

  tempValue = panaIntString(0x444, 4, true);
  assert(!strcmp(tempValue, "0444"));
  free(tempValue);

  tempValue = panaIntString(0x444, 5, true);
  assert(!strcmp(tempValue, "00444"));
  free(tempValue);

  tempValue = panaIntString(333, 2, false);
  assert(!strcmp(tempValue, "333"));
  free(tempValue);

  tempValue = panaIntString(333, 3, false);
  assert(!strcmp(tempValue, "333"));
  free(tempValue);

  tempValue = panaIntString(333, 4, false);
  assert(!strcmp(tempValue, "0333"));
  free(tempValue);

  tempValue = panaIntString(333, 5, false);
  assert(!strcmp(tempValue, "00333"));
  free(tempValue);

  fprintf(stderr, "Done.\n");
}

#endif  // USE_PANASONIC_PTZ && !ENABLE_P2_MODE

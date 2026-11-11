#include "p2protocol.h"

#include <arpa/inet.h>
#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include <libxml/parser.h>  // -lxml2

#include <openssl/md5.h>  // -lcrypto

#include "main.h"
#include "configurator.h"
#include "constants.h"
#include "panasonic_shared.h"

#if USE_PANASONIC_PTZ && ENABLE_P2_MODE  // Otherwise, this .c file is a no-op.

// #define P2_APP_NAME "VISCAPTZ"
#define P2_APP_NAME "sRC_SemiProApp_iOS"

#pragma mark - Data types

/** A sized buffer for passing around data from libcurl. */
typedef struct {
  char *data;
  size_t len;
} curl_buffer_t;

/** A motor/zoom position at a given time. */
typedef struct {
  int64_t position;
  double timeStamp;
} timed_position_t;

static const int panasonic_p2_udp_port = 49153;
static const int panasonic_p2_tcp_port = 49152;


#pragma mark - Function prototypes

/** The number of zoom positions moved in one second at the specified speed. */
int64_t panaZoomPositionsPerSecondAtSpeed(int speed);
void markSpeedChange(int newSpeed);
int64_t estimatedPositionsSinceLastPositionData(void);
void freeSpeedChanges(void);

int64_t p2GetZoomPositionRaw(void);
char *md5_string(uint8_t *hashbuf);
char *getP2Auth(int sock);

char *generateCamControlRequest(const char *command);
bool sendP2Request(char *request, char **buf);
bool requestEnv(char *authToken, int sock);

char *p2IntString(uint64_t value, int digits, bool hex);

char *textForResponseElement(char *tag, char *result);

void populateZoomNonlinearityTable(void);

void runP2Tests(void);

ssize_t syncRead(int fd, void *buf, size_t count);
ssize_t syncReadLong(int fd, char *buf, size_t capacity);

#pragma mark - Global variables

static pthread_mutex_t gSocketLock, gSpeedLock;
static pthread_cond_t gZoomDataCond;

static int gP2UDPSocket = -1;
static int gP2TCPSocket = -1;
struct sockaddr_in gP2Addr;

static pthread_t gP2UDPThread;
void *runP2UDPThread(void *argIgnored);

/** If true, enables extra debugging. */
static bool p2_enable_debugging = false;

/** The last zoom speed that was set. */
static int64_t gLastZoomSpeed = 0;

/** The last zoom position received. */
static int64_t gLastZoomPosition = 0;

/** The last tally state received. */
static int64_t gLastTallyState = 0;

/** The IP address of the camera. */
static char *g_cameraIPAddr = NULL;

static char *gAuthToken = NULL;
static char *gSessionID = NULL;

/** Cached minimum and maximum hardware zoom speeds queried from the camera via $ZmSpd:c. */
static int gMinZoomSpeed = 0;
static int gMaxZoomSpeed = 0;

#if USE_PANASONIC_PTZ
  /** The zoom calibration data in raw form (positions per second for each speed). */
  int64_t *p2ZoomCalibrationData = NULL;

  /** The zoom calibration data in core-scaled form (1000 * value / max_value). */
  int32_t *p2ScaledZoomCalibrationData = NULL;
#endif  // USE_PANASONIC_PTZ

// This assumes the compiler allocates bits from low to high.
typedef struct p2_optical_data {
  uint8_t type;
  uint8_t size;
  uint8_t reserved_2;
  uint8_t reserved_3;
  uint8_t reserved_4;
  uint8_t focus_ft_high;
  uint8_t focus_ft_low;
  uint8_t focus_in;
  uint8_t iris_high;
  uint8_t iris_low;

  uint8_t focus_sig_high : 4;
  uint8_t focus_exp : 4;

  uint8_t focus_sig_low;

  uint8_t zoom_sig_high : 4;
  uint8_t zoom_exp : 4;

  uint8_t zoom_sig_low;
  uint8_t lens_model_name[30];
  uint8_t master_gain_high;
  uint8_t master_gain_low;
  uint8_t shutter_speed_high;
  uint8_t shutter_speed_low;

  uint8_t shutter_mode : 4;
  uint8_t reserved_48 : 4;

  uint8_t shutter_speed_decimal;
  uint8_t gamma_mode;
  uint8_t reserved_51;

  uint8_t color_temp_high : 4;
  uint8_t under_over : 2;
  uint8_t atw_wb : 2;

  uint8_t color_temp_low;
  uint8_t nd_filter;
  uint8_t cc_filter;

  uint8_t model_info : 2;
  uint8_t zoom_info : 2;
  uint8_t focus_info : 2;
  uint8_t iris_info : 2;

  uint8_t nd_disp_type : 1;
  uint8_t iZoom_sm : 1;
  uint8_t iris_type : 1;
  uint8_t rb_gain_info : 1;
  uint8_t iA_zoom_info : 2;
  uint8_t gain_mode : 1;
  uint8_t agc_enabled : 1;

  uint8_t vfr_frame_rate_high : 4;
  uint8_t vfr_mode : 4;
  uint8_t vfr_frame_rate_low;
  uint8_t iA_zoom_high : 4;
  uint8_t iA_zoom_exp : 4;
  uint8_t iA_zoom_low;

  uint8_t awb_channel : 1;
  uint8_t color_temp_mag : 1;
  uint8_t awb_color_temp : 1;
  uint8_t e1_gain_mode : 1;
  uint8_t iso_select : 4;

  uint8_t color_temp_GMg_high;
  uint8_t color_temp_GMg_low;
} p2_optical_data_t;

typedef struct p2_camera_data {
  uint8_t type;
  uint8_t size;
  uint8_t junk1;
  uint8_t junk2;
  uint8_t tally_and_thumbnail;
  uint8_t junk3[30];
} p2_camera_data_t;

typedef struct speed_time {
  double timestamp;
  int speed;
  struct speed_time *next;
} speed_time_t;

speed_time_t *gSpeedHead;

#pragma mark - P2 module implementation

// Public function.  Docs in header.
//
// Initializes the module.
bool p2ModuleInit(void) {
  pthread_mutex_init(&gSocketLock, NULL);
  pthread_mutex_init(&gSpeedLock, NULL);
  pthread_cond_init(&gZoomDataCond, NULL);

  assert(sizeof(p2_optical_data_t) == 65);

  // fprintf(stderr, "p2ModuleInit\n");
  runP2Tests();
  // fprintf(stderr, "p2ModuleInit2\n");

  #if USE_PANASONIC_PTZ && ENABLE_P2_MODE
    if (p2_enable_debugging) fprintf(stderr, "P2 module init\n");

    gP2UDPSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (gP2UDPSocket == -1) {
        perror("Socket could not be created.");
        return false;
    }
    int reuseValue = 1;
    setsockopt(gP2UDPSocket, SOL_SOCKET, SO_REUSEADDR, &reuseValue, sizeof(reuseValue));

    // fprintf(stderr, "p2ModuleInit3\n");

    fprintf(stderr, "Connecting to camera at %s\n", g_cameraIPAddr);

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    inet_aton(g_cameraIPAddr, &sa.sin_addr);
    sa.sin_port = htons(panasonic_p2_udp_port);

    gP2Addr = sa;

    struct sockaddr_in sa_recv;
    bzero(&sa_recv, sizeof(sa_recv));
    sa_recv.sin_family = AF_INET;
    sa_recv.sin_addr.s_addr = INADDR_ANY;
    sa_recv.sin_port = htons(panasonic_p2_udp_port);
    if (bind(gP2UDPSocket, (struct sockaddr *)&sa_recv, sizeof(sa_recv)) != 0) {
        perror("Bind failed");
        close(gP2UDPSocket);
        gP2UDPSocket = -1;
        return false;
    }

    if (p2_enable_debugging) fprintf(stderr, "P2 module init done\n");
  #else
    if (p2_enable_debugging) fprintf(stderr, "P2 module init skipped\n");
  #endif
  // fprintf(stderr, "p2ModuleInit4\n");

  // int64_t zp = p2GetZoomPositionRaw();
  // fprintf(stderr, "zpr: %" PRId64 "\n", zp);

  populateZoomNonlinearityTable();
  return p2ModuleReload();
}

bool p2ModuleStart(void) {
  #if USE_PANASONIC_PTZ && ENABLE_P2_MODE
    pthread_create(&gP2UDPThread, NULL, runP2UDPThread, NULL);
  #endif
  return true;
}

// Public function.  Docs in header.
//
// Reinitializes the module after calibration.
bool p2ModuleReload(void) {
    fprintf(stderr, "Loading configuration.\n");
  #if USE_PANASONIC_PTZ
    int maxSpeed = 0;
    bool localDebug = false;
    panasonicSharedInit(&p2ZoomCalibrationData, &maxSpeed);
    int minZoomSpeed = ZOOM_MIN_HARDWARE;
    int maxZoomSpeed = ZOOM_SCALE_HARDWARE;
    if (p2ZoomCalibrationData != NULL && maxZoomSpeed > 0 &&
        maxSpeed == maxZoomSpeed - minZoomSpeed) {
        p2ScaledZoomCalibrationData =
            convertSpeedValues(p2ZoomCalibrationData, maxSpeed,
                               axis_identifier_zoom);
        if (localDebug) {
          for (int i=0; i<=maxSpeed; i++) {
            fprintf(stderr, "%d: raw: %" PRId64 "\n", i, p2ZoomCalibrationData[i]);
            fprintf(stderr, "%d: scaled: %d\n", i, p2ScaledZoomCalibrationData[i]);
          }
        }
    } else {
        fprintf(stderr, "Ignoring calibration data because the scale (%d) is incorrect.\n",
                maxSpeed);
    }
  #endif  // USE_PANASONIC_PTZ
  return true;
}

// Public function.  Docs in header.
//
// Tears down the module.
bool p2ModuleTeardown(void) {
  if (gP2TCPSocket != -1) {
    close(gP2TCPSocket);
    gP2TCPSocket = -1;
  }
  if (gP2UDPSocket != -1) {
    close(gP2UDPSocket);
    gP2UDPSocket = -1;
  }
  return true;
}

// Public function.  Docs in header.
//
// Sets the camera's IP address.
bool p2SetIPAddress(char *address) {
  if (g_cameraIPAddr != NULL) {
    free(g_cameraIPAddr);
  }
  asprintf(&g_cameraIPAddr, "%s", address);
  return true;
}

// Public function.  Docs in header.
//
// Returns true and populates the current minimum and maximum zoom position values, or
// returns false if there is no calibration data.
bool p2GetZoomRange(int64_t *min, int64_t *max) {
  // Don't trust the range if there's no calibration data.
  if (p2ScaledZoomCalibrationData == NULL) {
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
// Returns the camera's most recent zoom speed (for debugging only).
int64_t p2GetZoomSpeed(void) {
    // Just return the last cached value.  The camera doesn't provide
    // a way to query this.
    return gLastZoomSpeed;
}

// Queries $ZmSpd:c and caches the minimum and maximum zoom speeds.
//
// Message:
// <P2Control><Auth>5029c78a681b8bdb80ab868bbabb1393</Auth><SessionID>007</SessionID><CamCtl>$ZmSpd:c</CamCtl></P2Control>
//
// Response:
// <P2Control><Response><CamCtl>$ZmSpd:i20,30000</CamCtl></Response></P2Control>
//
// Valid speeds for '$ZmSpd:=i'max','min are -max..-min, 0, and min..max.
static bool p2FetchZoomSpeedLimits(void) {
  if (gMaxZoomSpeed > 0) {
    return true;
  }

  bool localDebug = false || p2_enable_debugging;
  char *request = generateCamControlRequest("$ZmSpd:c");
  char *result = NULL;
  bool ok = sendP2Request(request, &result);
  free(request);
  if (!ok) {
    fprintf(stderr, "Could not send request.\n");
    return false;
  }

  if (localDebug) fprintf(stderr, "Zoom speed response: %s\n", result);

  char *text = textForResponseElement("CamCtl", result);
  free(result);

  if (localDebug) fprintf(stderr, "Zoom speed result: %s\n", text);

  bool parsed = false;
  if (!strncmp(text, "$ZmSpd:i", 8)) {
    char *minPart = text + 8;
    if (localDebug) fprintf(stderr, "Speed part: \"%s\"\n", minPart);
    char *comma = NULL;
    long minSpeed = strtol(minPart, &comma, 10);
    if (comma != minPart && *comma == ',') {
      char *maxPart = comma + 1;
      char *endPtr = NULL;
      long maxSpeed = strtol(maxPart, &endPtr, 10);
      if (endPtr != maxPart && *endPtr == '\0' &&
          minSpeed >= 0 && maxSpeed > 0 && maxSpeed >= minSpeed) {
        gMinZoomSpeed = (int)minSpeed;
        gMaxZoomSpeed = (int)maxSpeed;
        parsed = true;
      }
    }
  }
  free(text);
  return parsed;
}

// Gets the minimum zoom speed.
int p2GetMinZoomSpeed(void) {
  p2FetchZoomSpeedLimits();
  return gMinZoomSpeed;
}

// Gets the maximum zoom speed.
int p2GetMaxZoomSpeed(void) {
  p2FetchZoomSpeedLimits();
  return gMaxZoomSpeed;
}

// Public function.  Docs in header.
//
// Sets the camera's zoom speed.

bool p2SetZoomSpeed(int64_t speed, bool isRaw) {
    bool localDebug = false || p2_enable_debugging;

    int minZoomSpeed = ZOOM_MIN_HARDWARE;
    int maxZoomSpeed = ZOOM_SCALE_HARDWARE;
    if (maxZoomSpeed <= 0) {
      fprintf(stderr, "Could not determine zoom speed range.\n");
      return false;
    }

    int intSpeed = 0;
    if (isRaw) {
      intSpeed = speed;
    } else if (speed != 0) {
      if (p2ScaledZoomCalibrationData == NULL) {
        int offset = (speed < 0) ? -(minZoomSpeed - 1) : (minZoomSpeed - 1);
        intSpeed = scaleSpeed(speed, SCALE_CORE,
                              maxZoomSpeed - minZoomSpeed + 1, NULL) + offset;
      } else {
        int offset = (speed < 0) ? -minZoomSpeed : minZoomSpeed;
        intSpeed = scaleSpeed(speed, SCALE_CORE,
                              maxZoomSpeed - minZoomSpeed,
                              p2ScaledZoomCalibrationData) + offset;
      }
    }
    if (localDebug) {
        fprintf(stderr, "Zoom Core speed %" PRId64 "\n", speed);
        fprintf(stderr, "Zoom Speed %d\n", intSpeed);
    }

    bool retval = true;
    char *rawCommand = NULL;
    asprintf(&rawCommand, "$ZmSpd:=%d", intSpeed);
    char *command = generateCamControlRequest(rawCommand);
    free(rawCommand);
    char *response = NULL;
    bool ok = sendP2Request(command, &response);
    free(command);

    fprintf(stderr, "In p2SetZoomSpeed: response: %s\n", response);

    // Response violates the spec.  My camera just sends back an empty element.

    if (!ok /* || !response || atoi(response) != intSpeed */) {
        retval = false;
    } else {
        gLastZoomSpeed = intSpeed;
        markSpeedChange(intSpeed);
    }
    free(response);
    return retval;
}

// Similar to p2GetZoomPosition, but does not correct for the nonlinearity of
// the position data.  [redacted swearing at Panasonic]
int64_t p2GetZoomPositionRaw(void) {
    if (gCalibrationMode) {
      // Wait for the next value to arrive from the camera.  There's really no point in
      // taking this mutex, because we know the value won't change quickly (twice per second),
      // but the pthreads API doesn't allow condition waits without a mutex, so we'll just burn
      // the extra cycles.
      struct timeval now;
      gettimeofday(&now, NULL);
      struct timespec timeout;
      timeout.tv_sec = now.tv_sec + 5;
      timeout.tv_nsec = now.tv_usec * 1000;

      pthread_mutex_lock(&gSpeedLock);
      int err = pthread_cond_timedwait(&gZoomDataCond, &gSpeedLock, &timeout);
      pthread_mutex_unlock(&gSpeedLock);
      if (err != 0) {
        fprintf(stderr, "Timed out waiting for P2 UDP zoom position broadcast during calibration.  Bailing.\n");
        p2SetZoomSpeed(0, true);
        exit(1);
      }
    }

    return gLastZoomPosition;
}

// Public function.  Docs in header.
//
// Gets the camera's current zoom position.
int64_t p2GetZoomPosition(void) {
    bool localDebug = false;
    int64_t nonlinearZoomPosition = p2GetZoomPositionRaw();
    fprintf(stderr, "ZOOMNONLINEARPOS: %" PRId64 "\n", nonlinearZoomPosition);
    int64_t retval = panaMakeZoomLinear(nonlinearZoomPosition);
    if (localDebug) {
        fprintf(stderr, "ZOOMPOS: %" PRId64 "\n", retval);
    }

    if (!gCalibrationMode) {
      int64_t zoomMin, zoomMax;
      if (p2GetZoomRange(&zoomMin, &zoomMax)) {
        retval += estimatedPositionsSinceLastPositionData();
        retval = MAX(zoomMin, retval);
        retval = MIN(zoomMax, retval);
      }
    }

    return retval;
}

// Public function.  Docs in header.
//
// Returns the camera's current tally light state.
int p2GetTallyState(void) {
  // If we actually cared about getting changes more instantly, we could query this directly
  // by calling $RTlySw:? and $GTlySw:?, but it isn't worth the effort when we're getting
  // both values twice per second anyway.

  return gLastTallyState;
}

// Public function.  Docs in header.
//
// Sets the camera's tally light state.
bool p2SetTallyState(int tallyState) {
  char *redRequest = generateCamControlRequest(
      (tallyState == kTallyStateRed) ? "$RTlySw:=On" : "$RTlySw:=Off");
  char *greenRequest = generateCamControlRequest(
      (tallyState == kTallyStateGreen) ? "$GTlySw:=On" : "$GTlySw:=Off");

  char *junkbuf = NULL;
  bool redOk = sendP2Request(redRequest, &junkbuf);
  free(redRequest);
  if (junkbuf != NULL) free(junkbuf);

  junkbuf = NULL;
  bool greenOk = sendP2Request(greenRequest, &junkbuf);
  free(greenRequest);
  if (junkbuf != NULL) free(junkbuf);

  return redOk && greenOk;
}


#pragma mark - Calibration

// Public function.  Docs in header.
//
// Creates speed tables indicating how fast the zoom axis moves at various
// speeds (in positions per second).
void p2ModuleCalibrate(void) {
  panaModuleCalibrate();
}

// Public function.  Docs in header.
//
// Returns the number of zoom positions per second at the camera's slowest speed.
int64_t p2MinimumZoomPositionsPerSecond(void) {
  return panaMinimumZoomPositionsPerSecond();
}

// Public function.  Docs in header.
//
// Returns the number of zoom positions per second at the camera's fastest speed.
int64_t p2MaximumZoomPositionsPerSecond(void) {
  return panaMaximumZoomPositionsPerSecond();
}


#pragma mark - Helper methods

int openP2Socket(void) {
  fprintf(stderr, "Opening P2 socket.\n");
  int sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    perror("socket");
    return -1;
  }
  struct sockaddr_in sa;
  bzero(&sa, sizeof(sa));

  sa.sin_family = AF_INET;
  sa.sin_port = htons(panasonic_p2_tcp_port);
  inet_aton(g_cameraIPAddr, &sa.sin_addr);

  if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        perror("connect");
        printf("Connection with the server failed...\n");
        close(sock);
        return -1;
  }

  char *authToken = getP2Auth(sock);
  if (authToken == NULL) {
    fprintf(stderr, "P2 auth failed.\n");
    close(sock);
    return -1;
  }

  if (!requestEnv(authToken, sock)) {
    fprintf(stderr, "Request environment failed.\n");
    close(sock);
    return -1;
  }

  return sock;
}

char *getP2Auth(int sock) {
  bool localDebug = false;
  char *message = NULL;
  char *user = getenv("PANA_USER");
  char *password = getenv("PANA_PASS");

  if (!user || !password) {
    fprintf(stderr, "This feature is in development.  For now, you must set the\n"
                    "PANA_USER and PANA_PASS environment variables.\n");
    exit(1);
  }

  if (localDebug) fprintf(stderr, "Beginning auth handshake.\n");

  asprintf(&message, "<P2Control><Login>%s</Login></P2Control>", user);

  if (localDebug) fprintf(stderr, "User: %s\n", user);

  ssize_t expectedLength = strlen(message);
  ssize_t length = write(sock, message, expectedLength);
  free(message);
  if (length != expectedLength) {
    perror("write");
    fprintf(stderr, "Could not write auth message 1; got %" PRId64 "\n", (uint64_t)length);
    return NULL;
  }

  char buf[4097];

  length = syncReadLong(sock, buf, 4096);
  if (length <= 0) {
    fprintf(stderr, "Short read.\n");
    return NULL;
  }

  if (localDebug) fprintf(stderr, "Parsing response\n");

  xmlDocPtr doc = xmlReadMemory(buf, length, "response.xml", NULL, 0);
  if (doc == NULL) {
    fprintf(stderr, "Could not parse auth response.\n");
    return NULL;
  }

  // <P2Control><Response><Realm>...</Realm><Nonce>...</Nonce></Response></P2Control>
  xmlNode *root_element = xmlDocGetRootElement(doc);
  xmlNode *response = (root_element != NULL) ? root_element->children : NULL;

  // Skip any whitespace/text nodes to find the <Response> element.
  while (response != NULL &&
         (response->type != XML_ELEMENT_NODE ||
          strcmp((const char *)response->name, "Response"))) {
    response = response->next;
  }
  if (response == NULL) {
    fprintf(stderr, "No <Response> element in auth response.\n");
    xmlFreeDoc(doc);
    return NULL;
  }

  xmlNode *curnode = response->children;
  char *realm = NULL;
  char *nonce = NULL;

  while (curnode) {
    if (!strcmp((const char *)curnode->name, "Realm")) {
      if (realm != NULL) {
        xmlFree(realm);
      }
      realm = (char *)xmlNodeGetContent(curnode);
    }
    if (!strcmp((const char *)curnode->name, "Nonce")) {
      if (nonce != NULL) {
        xmlFree(nonce);
      }
      nonce = (char *)xmlNodeGetContent(curnode);
    }
    curnode = curnode->next;
  }
  if (realm == NULL) {
    fprintf(stderr, "Missing realm in auth response.\n");
    if (nonce != NULL) xmlFree(nonce);
    xmlFreeDoc(doc);
    return NULL;
  }
  if (nonce == NULL) {
    fprintf(stderr, "Missing nonce in auth response.\n");
    if (realm != NULL) xmlFree(realm);
    xmlFreeDoc(doc);
    return NULL;
  }

  if (localDebug) fprintf(stderr, "Realm: \"%s\"\nNonce: \"%s\"\n", realm, nonce);

  if (localDebug) fprintf(stderr, "Handshake stage 2\n");

  char *encryptedPasswordStage1 = NULL;
  asprintf(&encryptedPasswordStage1, "%s:%s:%s", user, realm, password);

  if (localDebug) fprintf(stderr, "Encryption 1: %s\n", encryptedPasswordStage1);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  // unsigned char *MD5(const unsigned char *d, unsigned long n, unsigned char *md);
  uint8_t hash[MD5_DIGEST_LENGTH];
  MD5((const unsigned char *)encryptedPasswordStage1, strlen(encryptedPasswordStage1), hash);
  free(encryptedPasswordStage1);

  char *hashstring = md5_string(hash);

  char *encryptedPasswordStage2 = NULL;
  asprintf(&encryptedPasswordStage2, "%s:%s", hashstring, nonce);
  free(hashstring);
  if (localDebug) fprintf(stderr, "Encryption 2: %s\n", encryptedPasswordStage2);

  MD5((const unsigned char *)encryptedPasswordStage2, strlen(encryptedPasswordStage2), hash);
  free(encryptedPasswordStage2);

#pragma GCC diagnostic pop
#pragma clang diagnostic pop

  hashstring = md5_string(hash);

  if (localDebug) fprintf(stderr, "Encryption 3: %s\n", hashstring);

  xmlFree(realm);
  xmlFree(nonce);
  xmlFreeDoc(doc);

  return hashstring;
}

bool requestEnv(char *authToken, int sock) {
  char *message = NULL;
  asprintf(&message, "<P2Control><Auth>%s</Auth><Query Type=\"env\"/></P2Control>", authToken);
  if (gAuthToken != NULL) {
    free(gAuthToken);
  }
  gAuthToken = authToken;

  ssize_t expectedLength = strlen(message);
  ssize_t length = write(sock, message, expectedLength);
  free(message);
  message = NULL;
  if (length != expectedLength) {
    perror("write");
    fprintf(stderr, "Could not write auth message 2; got %" PRId64 "\n", (uint64_t)length);
    return false;
  }

  char buf[4097];
  length = syncReadLong(sock, buf, 4096);
  if (length <= 0) return false;

  buf[length] = '\0';

  fprintf(stderr, "Environment:\n%s\n", buf);

  asprintf(&message, "<P2Control><Auth>%s</Auth><SessionID></SessionID><CamCtl>$Connect:=On</CamCtl><CamCtl>$MyName:%s</CamCtl></P2Control>", authToken, P2_APP_NAME);

  fprintf(stderr, "Auth message: %s", message);

  expectedLength = strlen(message);
  length = write(sock, message, expectedLength);
  free(message);
  message = NULL;
  if (length != expectedLength) {
    fprintf(stderr, "Could not write auth message 3; got %" PRId64 "\n", (uint64_t)length);
    return false;
  }

  length = syncReadLong(sock, buf, 4096);
  if (length <= 0) {
    fprintf(stderr, "No response from camera.\n");
    return false;
  }

  fprintf(stderr, "Parsing auth message.\n");
  xmlDocPtr doc = xmlReadMemory(buf, length, "response.xml", NULL, 0);
  if (doc == NULL) {
    fprintf(stderr, "Could not parse environment/connect response.\n");
    return false;
  }

  xmlNode *root_element = xmlDocGetRootElement(doc);
  xmlNode *camctl = (root_element != NULL) ? root_element->children : NULL;
  while (camctl != NULL && camctl->type != XML_ELEMENT_NODE) {
    camctl = camctl->next;
  }

  if (camctl == NULL || strcmp((const char *)camctl->name, "CamCtl")) {
    fprintf(stderr, "Invalid first tag %s in response\n",
            camctl ? (const char *)camctl->name : "<none>");
    xmlFreeDoc(doc);
    return false;
  }
  for (xmlAttr *attribute = camctl->properties; attribute ; attribute = attribute->next) {
    if (!strcmp((const char *)attribute->name, "SessionID")) {
      xmlChar *attributeValue = xmlNodeListGetString(doc, attribute->children, 1);
      free(gSessionID);
      gSessionID = NULL;
      asprintf(&gSessionID, "%s", (const char *)attributeValue);
      xmlFree(attributeValue);
    }
  }
  fprintf(stderr, "Session ID: \"%s\"\n", gSessionID);
  buf[length] = '\0';
  fprintf(stderr, "Message was: %s\n", buf);

  xmlFreeDoc(doc);
  return true;
}

char *md5_string(uint8_t *hashbuf) {
  char *buf = NULL;
  asprintf(&buf, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
    hashbuf[0], hashbuf[1], hashbuf[2], hashbuf[3], hashbuf[4], hashbuf[5], 
    hashbuf[6], hashbuf[7], hashbuf[8], hashbuf[9], hashbuf[10], hashbuf[11], 
    hashbuf[12], hashbuf[13], hashbuf[14], hashbuf[15]);
  return buf;
}

char *generateCamControlRequest(const char *command) {
  char *request = NULL;
  asprintf(&request, "<CamCtl>%s</CamCtl>", command);
  return request;
}

bool sendP2Request(char *request, char **buf) {
  if (buf == NULL) return false;
  *buf = NULL;

  static double timestampForCrappyFirmware = 0;
  pthread_mutex_lock(&gSocketLock);

  fprintf(stderr, "sendP2Request point 1\n");

  // 30 second limit
  fprintf(stderr, "TS: %lf %lf\n", timeStamp(), timestampForCrappyFirmware);

  if (gP2TCPSocket == -1 || (timeStamp() - timestampForCrappyFirmware) > 10) {
    if (gP2TCPSocket != -1) {
      fprintf(stderr, "Forcing socket closed (time limit expired).\n");
      close(gP2TCPSocket);
      gP2TCPSocket = -1;
    }
    gP2TCPSocket = openP2Socket();
    fprintf(stderr, "Set gP2TCPSocket to %d\n", gP2TCPSocket);
    timestampForCrappyFirmware = timeStamp();
  }

  char *authToken = gAuthToken; //getP2Auth(gP2TCPSocket);
  if (authToken == NULL) {
    fprintf(stderr, "Could not get auth token for sendP2Request\n");
    pthread_mutex_unlock(&gSocketLock);
    return false;
  }

  char *message = NULL;
  asprintf(&message, "<P2Control><Auth>%s</Auth><SessionID>%s</SessionID>%s</P2Control>", authToken, gSessionID, request);

  fprintf(stderr, "In sendP2Request: %s", message);

  ssize_t expectedSize = strlen(message);
  ssize_t size = write(gP2TCPSocket, message, expectedSize);

  free(message);

  fprintf(stderr, "sendP2Request point 2 %zu %zu\n", size, expectedSize);

  if (size < 0) {
    pthread_mutex_unlock(&gSocketLock);
    return false;
  }
  bool retval = true;
  if (size != expectedSize) {
    retval = false;
  }

  fprintf(stderr, "In sendP2Request: Waiting for response.\n");
  char *localBuf = (char *)malloc(4097);
  ssize_t length = syncRead(gP2TCPSocket, localBuf, 4096);

  if (!retval || length <= 0) {
    free(localBuf);
    pthread_mutex_unlock(&gSocketLock);
    return false;
  }

  localBuf[length] = '\0';
  *buf = localBuf;

  fprintf(stderr, "In sendP2Request: Got response %s\n", localBuf);

  pthread_mutex_unlock(&gSocketLock);
  return true;
}

// Helper function that computes the zoom position from a P2 data packet.
int64_t zoomPositionFromData(p2_optical_data_t *optical_data) {

#if 0
    uint64_t exponent = optical_data->zoom_exp;  // 1..15 repeating, but starts at 3.
    uint64_t high = optical_data->zoom_sig_high - 11;  // 0 or 1.
    uint64_t rough_value = ((high * 15) + exponent) - 3;
    uint64_t low = optical_data->zoom_sig_low;  // 0..255.  Starts at 112.

    // WTF is this?

#endif
    // This is a signed number crammed into four bits.
    int8_t exp = optical_data->zoom_exp - 10;
    // if (exp & 0x8) {
      // exp = -16 + exp;
    // }
    uint64_t mantissa = (optical_data->zoom_sig_high << 8) | optical_data->zoom_sig_low;

    uint64_t value = mantissa * powl(10, exp);


    fprintf(stderr, "Exp %d High %d Low %d Value %010" PRId64 "\n", optical_data->zoom_exp, optical_data->zoom_sig_high, optical_data->zoom_sig_low, value);

    // fprintf(stderr, "Exp %" PRId64 " High %" PRId64 " Rough %" PRId64 " Low %" PRId64 "\n", exponent, high, rough_value, low);

    return value;

    // int zoomPosition = ((rough_value << 8) + low) - 112;
    // return zoomPosition;
}

// Helper function that tells the Panasonic camera to start providing its zoom position.
// This should be called about once every 5 seconds.  If you do not call it for 15
// seconds, the camera stops providing zoom position data.
bool enableZoomPositionupdates(void) {
  char requestBuf[3] = { 0xff, 0x01, 0xff };

  ssize_t length = sendto(gP2UDPSocket, requestBuf, 3, 0, (struct sockaddr *)&gP2Addr, sizeof(gP2Addr));
  fprintf(stderr, "Write returned %zu on sock %d\n", length, gP2UDPSocket);

  return length == 3;
}

bool timeToRequestZoomPosition(void) {
  static double lastUpdateTimestamp = 0;
  double timestamp = timeStamp();

  if (timestamp - lastUpdateTimestamp >= 3.0) {
    lastUpdateTimestamp = timestamp;
    return true;
  }
  return false;
}

void updateZoomPositionAndTallyFromP2Data(uint8_t *packetBuffer, size_t packetSize) {
  if (packetSize == 0) {
    return;
  }
  uint8_t message_type = packetBuffer[0];
  if (message_type == 6) {
    if (packetSize < sizeof(p2_optical_data_t)) {
      fprintf(stderr, "Ignoring short packet for message type 6 (optical data).\n");
      return;
    }

    int64_t expectedPosition = gLastZoomPosition + estimatedPositionsSinceLastPositionData();
    // fprintf(stderr, "Zoom position message type\n");

    p2_optical_data_t *optical_data = (p2_optical_data_t *)packetBuffer;
    if (optical_data->zoom_info != 0) {
      fprintf(stderr, "Ignoring provided zoom value because it is invalid.\n");
      return;
    }

    gLastZoomPosition = zoomPositionFromData(optical_data);
    fprintf(stderr, "ZOOM POSITION NOW %lld\n", (long long)gLastZoomPosition);

    fprintf(stderr, "@@@ Zoom position: %" PRId64 " expected %" PRId64 "\n",
        gLastZoomPosition, expectedPosition);

    freeSpeedChanges();
    pthread_cond_broadcast(&gZoomDataCond);
  } else if (message_type == 0x0A) {
    if (packetSize < sizeof(p2_camera_data_t)) {
      fprintf(stderr, "Ignoring short packet for message type 0xA (camera data).\n");
      return;
    }
    p2_camera_data_t *camera_data = (p2_camera_data_t *)packetBuffer;
    // Red tally is bit 0 (value & 0x1).
    // Green tally is bit 1. (value & 0x2).
    // Return 5 for red (active).
    // Return 6 for green (preview).
    bool redState = (camera_data->tally_and_thumbnail & 0x01);
    bool greenState = (camera_data->tally_and_thumbnail & 0x02);
    gLastTallyState = redState ? kTallyStateRed : greenState ? kTallyStateGreen : 0;
  }
}

void *runP2UDPThread(void *argIgnored) {
  while (true) {
    // fprintf(stderr, "In runP2UDPThread\n");
    if (timeToRequestZoomPosition()) {
      // fprintf(stderr, "Fetching\n");
      if (!enableZoomPositionupdates()) {
        fprintf(stderr, "Error: Could not enable zoom position updates.\n");
      }
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(gP2UDPSocket, &read_fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 0.1 second wakeup
    int retval = select(gP2UDPSocket + 1, &read_fds, NULL, NULL, &tv);

    if (retval > 0) {
      // fprintf(stderr, "Got UDP response.  Reading data.\n");
      uint8_t buf[4096];
      struct sockaddr_in sender_addr;
      socklen_t sender_len = sizeof(sender_addr);
      ssize_t length = recvfrom(gP2UDPSocket, buf, sizeof(buf),
               0, (struct sockaddr *)&sender_addr, &sender_len);
      // fprintf(stderr, "Length %ld\n", length);
      if (length > 0 &&
          sender_addr.sin_family == AF_INET &&
          sender_addr.sin_addr.s_addr == gP2Addr.sin_addr.s_addr) {
        updateZoomPositionAndTallyFromP2Data(buf, (size_t)length);
      }
    }
  }
}

ssize_t syncRead(int fd, void *buf, size_t count) {
  ssize_t len = 0;
  int retries = 0;
  while (len <= 0 && retries < 100) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;  // 0.01 second wakeup
    int retval = select(fd + 1, &read_fds, NULL, NULL, &tv);
    if (retval > 0) {
      len = read(fd, buf, count);
      if (len == 0) {
        // EOF: peer closed connection.
        break;
      }
      if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        break;
      }
    }
    retries++;
  }
  return len;
}

#pragma mark - Position estimation

/**
 * In theory, a TCP read can return a partial response (though this is not
 * typically the case on a LAN).  For safety, read until the closing P2Control
 * mark.
 */
ssize_t syncReadLong(int fd, char *buf, size_t capacity) {
  size_t total = 0;
  while (total < capacity - 1) {
    ssize_t length = syncRead(fd, buf + total, capacity - 1 - total);
    if (length <= 0) break;
    total += length;
    buf[total] = '\0';
    if (strstr(buf, "</P2Control>") != NULL) break;
  }
  buf[total] = '\0';
  return (total > 0) ? (ssize_t)total : -1;
}

/*
 * Panasonic's P2 protocol is poorly suited for this purpose.  It provides no mechanism
 * for synchronously retrieving the current position of the zoom.  Instead, it provides
 * only a single value every 0.5 seconds, which isn't nearly good enough for precise
 * motion.
 *
 * To compensate for this inadequacy, we store a linked list containing the timestamp
 * when each speed change occurred, then compute an estimate of how many (linearized)
 * zoom positions the lens should have moved during each of those periods at those
 * specific speeds, and add that to the last reported position, wiping this linked
 * list whenever the camera sends a new value.
 */

void markSpeedChangeNoLock(int newSpeed);

/* Logs the exact timestamp when the camera's zoom speed changed. */
void markSpeedChange(int newSpeed) {
  pthread_mutex_lock(&gSpeedLock);
  markSpeedChangeNoLock(newSpeed);
  pthread_mutex_unlock(&gSpeedLock);
}

/*
 * The core of markSpeedChange, which should be called only when the gSpeedLock mutex
 * is already held.
 */
void markSpeedChangeNoLock(int newSpeed) {
  double now = timeStamp();

  // Prune stale entries older than 30 seconds in case UDP broadcasts have stopped arriving.
  while (gSpeedHead != NULL && gSpeedHead->next != NULL &&
         (now - gSpeedHead->next->timestamp) > 30.0) {
    speed_time_t *stale = gSpeedHead;
    gSpeedHead = gSpeedHead->next;
    free(stale);
  }

  speed_time_t *newSpeedItem = malloc(sizeof(speed_time_t));
  newSpeedItem->timestamp = now;
  newSpeedItem->speed = newSpeed;
  newSpeedItem->next = NULL;

  speed_time_t *pos = NULL;
  for (pos = gSpeedHead; pos && pos->next; pos = pos->next);

  if (pos) {
    pos->next = newSpeedItem;
  } else {
    gSpeedHead = newSpeedItem;
  }
}

/*
 * Returns the estimated number of positions moved since the last time we received
 * position data from the camera (half a second).
 */
int64_t estimatedPositionsSinceLastPositionData(void) {
  double timestamp = timeStamp();
  pthread_mutex_lock(&gSpeedLock);

  double positions = 0.0;
  for (speed_time_t *pos = gSpeedHead; pos != NULL; pos = pos->next) {
    double nextTimestamp = (pos->next == NULL) ? timestamp : pos->next->timestamp;
    double sec = nextTimestamp - pos->timestamp;
    positions += (panaZoomPositionsPerSecondAtSpeed(pos->speed) * sec);
  }
  pthread_mutex_unlock(&gSpeedLock);

  // Truncate to the nearest whole number of positions.
  return positions;
}

/* Clears previous timestamped speed change data when the camera sends fresh zoom position data. */
void freeSpeedChanges(void) {
  pthread_mutex_lock(&gSpeedLock);
  speed_time_t *oldHead = gSpeedHead;
  gSpeedHead = NULL;

  // Create a speed change entry starting from the time when the zoom data became available.
  markSpeedChangeNoLock(gLastZoomSpeed);

  pthread_mutex_unlock(&gSpeedLock);
  for (speed_time_t *pos = oldHead; pos;) {
    speed_time_t *next = pos->next;
    free(pos);
    pos = next;
  }
}

#pragma mark - XML

char *textForResponseElement(char *tag, char *result) {
  char *resultBuf = NULL;
  xmlDocPtr doc = NULL;

  if (result == NULL) goto fail;
  if (tag == NULL) goto fail;

  doc = xmlReadMemory(result, strlen(result), "response.xml", NULL, 0);
  if (doc == NULL) goto fail;

  xmlNode *root_element = xmlDocGetRootElement(doc);  // P2Control
  if (root_element == NULL) goto fail;

  xmlNode *response = root_element->children;  // Response
  while (response != NULL && response->type != XML_ELEMENT_NODE) {
    response = response->next;
  }
  if (response == NULL) goto fail;
  xmlNode *startNode = (!strcmp((const char *)response->name, "Response")) ? response->children : response;

  for (xmlNode *curnode = startNode; curnode; curnode = curnode->next) {
    if (curnode->type == XML_ELEMENT_NODE && !strcmp((const char *)curnode->name, tag)) {
      xmlChar *content = xmlNodeGetContent(curnode);
      asprintf(&resultBuf, "%s", content ? (const char *)content : "");
      if (content != NULL) {
        xmlFree(content);
      }
      xmlFreeDoc(doc);
      return resultBuf;
    }
  }
fail:
  asprintf(&resultBuf, "");
  if (doc != NULL) {
    xmlFreeDoc(doc);
  }
  return resultBuf;
}

// Public function.  Docs in header. 
//
// Returns the number of zoom positions per second at the camera's fastest speed.
int64_t panaZoomPositionsPerSecondAtSpeed(int speed) {
  if (p2ScaledZoomCalibrationData == NULL || p2ZoomCalibrationData == NULL || speed == 0) {
    return 0;
  }
  int absSpeed = abs(speed);
  int minZoomSpeed = ZOOM_MIN_HARDWARE;
  int maxZoomSpeed = ZOOM_SCALE_HARDWARE;
  if (maxZoomSpeed > 0 && absSpeed >= minZoomSpeed && absSpeed <= maxZoomSpeed) {
    return p2ZoomCalibrationData[absSpeed - minZoomSpeed] * (speed < 0 ? -1 : 1);
  }
  return 0;
} 

#pragma mark - Tests

/** Runs some basic tests of miscellaneous routines. */
void runP2Tests(void) {
}

#endif  // USE_PANASONIC_PTZ && ENABLE_P2_MODE

#include "p2protocol.h"

#include <arpa/inet.h>
#include <assert.h>
#include <curl/curl.h>
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

bool sendP2Request(char *request, char **buf);
bool requestEnv(char *authToken, int sock);

char *p2IntString(uint64_t value, int digits, bool hex);

char *textForResponseElement(char *tag, char *result);

void populateZoomNonlinearityTable(void);

void runP2Tests(void);

ssize_t syncread(int fd, void *buf, size_t count);

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
  uint64_t timestamp;
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
        return -1;
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
        return -1;
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
    if (maxSpeed == ZOOM_SCALE_HARDWARE) {
        p2ScaledZoomCalibrationData =
            convertSpeedValues(p2ZoomCalibrationData, ZOOM_SCALE_HARDWARE,
                               axis_identifier_zoom);
        if (localDebug) {
          for (int i=0; i<=ZOOM_SCALE_HARDWARE; i++) {
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

// Gets the maximum zoom speed.
int p2GetMaxZoomSpeed(void) {
  // Query $ZmSpd:c and that gets the maximum value.  Then send $ZmSpd:=value where value
  // is 0..max or 0..-max.

  // Message:
  // <P2Control><Auth>5029c78a681b8bdb80ab868bbabb1393</Auth><SessionID>007</SessionID><CamCtl>$ZmSpd:c</CamCtl></P2Control>
  //
  // Response:
  // <P2Control><CamCtl>$ZmSpd:i-8,8</CamCtl></P2Control

  char *result = NULL;
  bool ok = sendP2Request("<CamCtl>$ZmSpd:c</CamCtl>", &result);
  if (!ok) {
    fprintf(stderr, "Could not send request.\n");
    return 0;
  }

  // <P2Control><Response><CamCtl>$ZmSpd:8</CamCtl></Response></P2Control>
  // <P2Control><Response><CamCtl>$ZmSpd:i-8,8</CamCtl></Response></P2Control>

  fprintf(stderr, "Zoom speed response: %s\n", result);

  char *text = textForResponseElement("CamCtl", result);

  fprintf(stderr, "Zoom speed result: %s\n", text);

  if (!strncmp(text, "$ZmSpd:", 7)) {
    char *speed = text + 7;
    // If intelligent zoom is active, skip the intelligent zoom speed and get the hardware zoom speed after the comma.
    fprintf(stderr, "Speed part: \"%s\"\n", speed);
    if (speed[0] == 'i') {
      speed = strstr(speed, ",");
      if (speed != NULL) {
        speed++;  // Return the character after the comma.
      } else {
        return 0;
      }
    }
    return atoi(speed);
  } else {
    return 0;
  }
}

// Public function.  Docs in header.
//
// Sets the camera's zoom speed.

bool p2SetZoomSpeed(int64_t speed, bool isRaw) {
    bool localDebug = false || p2_enable_debugging;
    gLastZoomSpeed = speed;

    // This is wrong.
    int intSpeed =
        isRaw ? speed : scaleSpeed(speed, SCALE_CORE, ZOOM_SCALE_HARDWARE,
                                          p2ScaledZoomCalibrationData);

    if (localDebug) {
        fprintf(stderr, "Zoom Core speed %" PRId64 "\n", speed);
        fprintf(stderr, "Zoom Speed %d\n", intSpeed - 50);
    }

    static int maxZoomSpeed = 0;
    if (maxZoomSpeed == 0) {
      maxZoomSpeed = p2GetMaxZoomSpeed();
      fprintf(stderr, "Max zoom speed %d expected %d\n", maxZoomSpeed, ZOOM_SCALE_HARDWARE);
      assert(maxZoomSpeed == ZOOM_SCALE_HARDWARE);
    }

    bool retval = true;
    char *command = NULL;
    asprintf(&command, "<CamCtl>$ZmSpd:=%d</CamCtl>", intSpeed);
    char *response = NULL;
    bool ok = sendP2Request(command, &response);

    fprintf(stderr, "In p2SetZoomSpeed: response: %s\n", response);

    // Response violates the spec.  My camera just sends back an empty element.

    if (!ok /* || !response || atoi(response) != intSpeed */) {
        retval = false;
        free(response);
    }
    markSpeedChange(intSpeed);
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
      pthread_mutex_lock(&gSpeedLock);
      pthread_cond_wait(&gZoomDataCond, &gSpeedLock);
      pthread_mutex_unlock(&gSpeedLock);
    }

    return gLastZoomPosition;
}

// Public function.  Docs in header.
//
// Gets the camera's current zoom position.
int64_t p2GetZoomPosition(void) {
    bool localDebug = true;
    int64_t nonlinearZoomPosition = p2GetZoomPositionRaw();
    fprintf(stderr, "ZOOMNONLINEARPOS: %" PRId64 "\n", nonlinearZoomPosition);
    int64_t retval = panaMakeZoomLinear(nonlinearZoomPosition);
    if (localDebug) {
        fprintf(stderr, "ZOOMPOS: %" PRId64 "\n", retval);
    }

    if (!gCalibrationMode) {
      retval += estimatedPositionsSinceLastPositionData();
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
  bool retval = true;

  char *redRequest = (tallyState == kTallyStateRed) ?  "$RTlySw:=On" : "$RTlySw:=Off";
  char *greenRequest = (tallyState == kTallyStateGreen) ?  "$GTlySw:=On" : "$GTlySw:=Off";

  char *junkbuf = NULL;
  sendP2Request(redRequest, &junkbuf);
  if (junkbuf != NULL) free(junkbuf);

  junkbuf = NULL;
  sendP2Request(greenRequest, &junkbuf);
  if (junkbuf != NULL) free(junkbuf);

  return retval;
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
        return -1;
  }

  char *authToken = getP2Auth(sock);
  if (authToken == NULL) {
    close(sock);
    return -1;
  }

  requestEnv(authToken, sock);

  return sock;
}

char *getP2Auth(int sock) {
  char *message = NULL;
  char *user = getenv("PANA_USER");

  fprintf(stderr, "Beginning auth handshake.\n");

  asprintf(&message, "<P2Control><Login>%s</Login></P2Control>", user);

  fprintf(stderr, "User: %s\n", user);

  ssize_t length = write(sock, message, strlen(message));
  if (length != strlen(message)) {
    perror("write");
    fprintf(stderr, "Could not write auth message 1; got %" PRId64 "\n", (uint64_t)length);
    return false;
  }

  char buf[4097];

  length = syncread(sock, buf, 4096);
  if (length <= 0) {
    fprintf(stderr, "Short read.\n");
    return false;
  }

  fprintf(stderr, "Parsing response\n");

  xmlDocPtr doc = xmlReadMemory(buf, length, "response.xml", NULL, 0);

  // <P2Control><Response><Realm>...</Realm><Nonce>...</Nonce></Response></P2Control>
  xmlNode *root_element = xmlDocGetRootElement(doc);
  xmlNode *response = root_element->children;
  xmlNode *curnode = response->children;
  const char *realm = NULL;
  const char *nonce = NULL;

  while (curnode) {
    if (!strcmp((const char *)curnode->name, "Realm")) {
      xmlNode *realmTextNode = curnode->children;
      if (realmTextNode != NULL) {
        realm = (const char *)xmlNodeGetContent(realmTextNode);
      }
    }
    if (!strcmp((const char *)curnode->name, "Nonce")) {
      xmlNode *nonceTextNode = curnode->children;
      if (nonceTextNode != NULL) {
        nonce = (const char *)xmlNodeGetContent(nonceTextNode);
      }
    }
    curnode = curnode->next;
  }

  fprintf(stderr, "Realm: \"%s\"\nNonce: \"%s\"\n", realm, nonce);

  fprintf(stderr, "Handshake stage 2\n");

  char *password = getenv("PANA_PASS");

  char *encryptedPasswordStage1 = NULL;
  asprintf(&encryptedPasswordStage1, "%s:%s:%s", user, realm, password);

  fprintf(stderr, "Encryption 1: %s\n", encryptedPasswordStage1);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  // unsigned char *MD5(const unsigned char *d, unsigned long n, unsigned char *md);
  uint8_t hash[MD5_DIGEST_LENGTH];
  MD5((const unsigned char *)encryptedPasswordStage1, strlen(encryptedPasswordStage1), hash);

  char *hashstring = md5_string(hash);

  char *encryptedPasswordStage2 = NULL;
  asprintf(&encryptedPasswordStage2, "%s:%s", hashstring, nonce);
  free(hashstring);
  fprintf(stderr, "Encryption 2: %s\n", encryptedPasswordStage2);

  MD5((const unsigned char *)encryptedPasswordStage2, strlen(encryptedPasswordStage2), hash);

#pragma clang diagnostic pop

  hashstring = md5_string(hash);

  fprintf(stderr, "Encryption 3: %s\n", hashstring);

  xmlFreeDoc(doc);

  return hashstring;
}

bool requestEnv(char *authToken, int sock) {
  char *message = NULL;
  asprintf(&message, "<P2Control><Auth>%s</Auth><Query Type=\"env\"/></P2Control>", authToken);
  gAuthToken = authToken;

  ssize_t length = write(sock, message, strlen(message));
  if (length != strlen(message)) {
    perror("write");
    fprintf(stderr, "Could not write auth message 2; got %" PRId64 "\n", (uint64_t)length);
    return false;
  }

  char buf[4097];
  length = syncread(sock, buf, 4096);
  if (length <= 0) return false;

  buf[length] = '\0';

  fprintf(stderr, "Environment:\n%s\n", buf);

  free(message);

  asprintf(&message, "<P2Control><Auth>%s</Auth><SessionID></SessionID><CamCtl>$Connect:=On</CamCtl><CamCtl>$MyName:%s</CamCtl></P2Control>", authToken, P2_APP_NAME);

  fprintf(stderr, "Auth message: %s", message);

  length = write(sock, message, strlen(message));
  if (length != strlen(message)) {
    fprintf(stderr, "Could not write auth message 3; got %" PRId64 "\n", (uint64_t)length);
    return false;
  }

  length = syncread(sock, buf, 4096);
  if (length <= 0) {
    fprintf(stderr, "No response from camera.\n");
    return false;
  }

  fprintf(stderr, "Parsing auth message.\n");
  xmlDocPtr doc = xmlReadMemory(buf, length, "response.xml", NULL, 0);

  xmlNode *root_element = xmlDocGetRootElement(doc);
  xmlNode *camctl = root_element->children;

  if (strcmp((const char *)camctl->name, "CamCtl")) {
    fprintf(stderr, "Invalid first tag %s in response\n", (const char *)camctl->name);
    xmlFreeDoc(doc);
    return false;
  }
  for (xmlAttr *attribute = camctl->properties; attribute ; attribute = attribute->next) {
    if (!strcmp((const char *)attribute->name, "SessionID")) {
      xmlChar *attributeValue = xmlNodeListGetString(doc, attribute->children, 1);
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

bool sendP2Request(char *request, char **buf) {
  if (buf == NULL) return false;

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
    return false;
  }

  char *message = NULL;
  asprintf(&message, "<P2Control><Auth>%s</Auth><SessionID>%s</SessionID>%s</P2Control>", authToken, gSessionID, request);

  fprintf(stderr, "In sendP2Request: %s", message);

  ssize_t expectedSize = strlen(message);
  ssize_t size = write(gP2TCPSocket, message, expectedSize);

  free(message);

  fprintf(stderr, "sendP2Request point 2 %zu %zu\n", size, expectedSize);
  pthread_mutex_unlock(&gSocketLock);

  if (size < 0) {
    return false;
  }
  bool retval = true;
  if (size != expectedSize) {
    retval = false;
  }

  fprintf(stderr, "In sendP2Request: Waiting for response.\n");
  char *localBuf = (char *)malloc(4096);
  ssize_t length = syncread(gP2TCPSocket, localBuf, 4096);
  *buf = localBuf;

  localBuf[length] = '\0';

  fprintf(stderr, "In sendP2Request: Got response %s\n", localBuf);

  return retval && (length > 0);
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
  uint8_t message_type = packetBuffer[0];
  if (message_type == 6) {
    int64_t expectedPosition = gLastZoomPosition + estimatedPositionsSinceLastPositionData();
    // fprintf(stderr, "Zoom position message type\n");

    p2_optical_data_t *optical_data = (p2_optical_data_t *)packetBuffer;
    if (optical_data->zoom_info != 0) {
      fprintf(stderr, "Ignoring provided zoom value because it is invalid.\n");
      return;
    }

    gLastZoomPosition = zoomPositionFromData(optical_data);
    fprintf(stderr, "ZOOM POSITION NOW %lld\n", gLastZoomPosition);

    fprintf(stderr, "@@@ Zoom position: %" PRId64 " expected %" PRId64 "\n",
        gLastZoomPosition, expectedPosition);

    freeSpeedChanges();
    pthread_cond_broadcast(&gZoomDataCond);
  } else if (message_type == 0x0A) {
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
      ssize_t length = recvfrom(gP2UDPSocket, buf, sizeof(buf),
               0, NULL, NULL);
      // fprintf(stderr, "Length %ld\n", length);
      updateZoomPositionAndTallyFromP2Data(buf, length);
    }
  }
}

ssize_t syncread(int fd, void *buf, size_t count) {
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
    }
    retries++;
  }
  return len;
}

#pragma mark - Position estimation

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
  speed_time_t *newSpeedItem = malloc(sizeof(speed_time_t));
  newSpeedItem->timestamp = timeStamp();
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
  uint64_t timestamp = timeStamp();
  pthread_mutex_lock(&gSpeedLock);

  double positions = 0.0;
  for (speed_time_t *pos = gSpeedHead; pos && pos->next; pos = pos->next) {
    uint64_t nextTimestamp = (pos->next == NULL) ? timestamp : pos->next->timestamp;
    uint64_t msec = nextTimestamp - pos->timestamp;
    positions += (panaZoomPositionsPerSecondAtSpeed(pos->speed) * msec) / 1000000.0;
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
  for (speed_time_t *pos = oldHead; pos && pos->next; pos = pos->next) {
    free(pos);
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
  if (response == NULL) goto fail;
  xmlNode *startNode = (!strcmp((const char *)response->name, "Response")) ? response->children : response;

  for (xmlNode *curnode = startNode; curnode; curnode = curnode->next) {
    if (!strcmp((const char *)curnode->name, tag)) {
      asprintf(&resultBuf, "%s", (const char *)xmlNodeGetContent(curnode));
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
  int absSpeed = abs(speed);
  if (speed > 0 && speed <= ZOOM_SCALE_HARDWARE) {
    return p2ScaledZoomCalibrationData[absSpeed] * (speed < 0 ? -1 : 1) ;
  } 
  return 0;
} 

#pragma mark - Tests

/** Runs some basic tests of miscellaneous routines. */
void runP2Tests(void) {
}

#endif  // USE_PANASONIC_PTZ && ENABLE_P2_MODE

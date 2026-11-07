#include <assert.h>
#include <stdatomic.h>

#include "configurator.h"
#include "constants.h"
#include "main.h"
#include "obs_tally.h"
#include "panasonicptz.h"  // SET_TALLY_STATE
#include "p2protocol.h"    // SET_TALLY_STATE

void runOBSTests(void);

// Install libgettally from here: https://github.com/dgatwood/v8-libwebsocket-obs-websocket
#if USE_OBS_TALLY_SOURCE
  #include <gettally.h>
#else
  // Stub in something so that it builds.
  #define kOBSWebSocketURLKey "SockURL"
  #define kOBSPasswordKey "Pass"
  static inline void runOBSTally(char *OBSWebSocketURL, char *password) { }
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static char kSourceSeparator = '|';

static bool obs_tally_debug = false;
#if USE_OBS_TALLY_SOURCE
  static pthread_t obs_tally_thread;
#endif
static _Atomic(tallyStateType) gCurrentOBSTallyState = kTallyStateOff;
static char **gTallySourceNames = NULL;
static int gNumberOfTallySourceNames = 0;

void *runOBSTallyThread(void *argIgnored);
void addSceneOnProgram(const char *sceneName);
void addSceneOnPreview(const char *sceneName, bool alsoOnProgram);
void markSceneInactive(const char *sceneName);
void splitSourceNames(char *sourceNameString, char ***outArray, int *outCount);

typedef struct tally_scene_item {
  char *scene_name;
  tallyStateType tallyState;
  struct tally_scene_item *next;
} tally_scene_item_t;

tally_scene_item_t *activeScenes = NULL;

void addActiveSceneWithName(const char *name, tallyStateType tallyState);
void removeActiveSceneWithName(const char *name);
static void updateTallyState(void);

// Public function.  Docs in header.
//
// Initializes the OBS tally module.
bool obsModuleInit(void) {
  runOBSTests();

  #if USE_OBS_TALLY_SOURCE
    setTallyOff();

    char *OBSWebSocketURL = getConfigKey(kOBSWebSocketURLKey);
    if (OBSWebSocketURL == NULL) {
      fprintf(stderr, "No OBS web socket URL specified.  Initialization failed.\n");
      fprintf(stderr, "Run 'viscaptz --setobsurl <URL>' to fix.\n");
      return false;
    }
    free(OBSWebSocketURL);

    char *tallySourceName = getConfigKey(kTallySourceName);

    if (tallySourceName == NULL) {
      fprintf(stderr, "No tally source name specified.  Initialization failed.\n");
      fprintf(stderr, "Run 'viscaptz --settallysourcename <URL>' to fix.\n");
      return false;
    }

    char *password = getConfigKey(kOBSPasswordKey);
    if (password == NULL) {
      fprintf(stderr, "WARNING: No OBS web socket password specified.\n");
      fprintf(stderr, "Run 'viscaptz --setobspass <password>' to fix.\n");
    } else {
      // Treat NULL as empty password, which could be correct.
      free(password);
    }

    splitSourceNames(tallySourceName, &gTallySourceNames, &gNumberOfTallySourceNames);
    free(tallySourceName);
  #endif  // USE_OBS_TALLY_SOURCE

  return true;
}

bool obsModuleStart(void) {
  #if USE_OBS_TALLY_SOURCE
    pthread_create(&obs_tally_thread, NULL, runOBSTallyThread, NULL);
  #endif
  return true;
}

/** Splits the source name value into an array. */
void splitSourceNames(char *sourceNameString, char ***outArray, int *outCount) {
  assert(sourceNameString != NULL);
  assert(outArray != NULL);
  assert(outCount != NULL);

  *outCount = 1;
  for (char *pos = sourceNameString; pos && *pos; pos++) {
    if (*pos == kSourceSeparator) {
      (*outCount)++;
    }
  }
  *outArray = (char **)malloc(*outCount * sizeof(char *));
  char *startPos = sourceNameString;

  for (int i = 0; i < *outCount; i++) {
    char *endPos = NULL;
    for (endPos = startPos; endPos && *endPos && *endPos != kSourceSeparator; endPos++) {}
    uintptr_t count = endPos - startPos;
    char *copy = malloc(count + 1);
    strncpy(copy, startPos, count);
    copy[count] = '\0';
    (*outArray)[i] = copy;
    startPos = endPos + 1;
  }
}

// Public function.  Docs in header.
//
// Main loop of the OBS tally module listener thread.
void *runOBSTallyThread(void *argIgnored) {
  char *OBSWebSocketURL = getConfigKey(kOBSWebSocketURLKey);
  char *password = getConfigKey(kOBSPasswordKey) ?: "";

  #if USE_OBS_TALLY_SOURCE
    registerOBSProgramCallback(&addSceneOnProgram);
    registerOBSPreviewCallback(&addSceneOnPreview);
    registerOBSInactiveCallback(&markSceneInactive);
  #endif  // USE_OBS_TALLY_SOURCE

  // Function never returns.
  runOBSTally(OBSWebSocketURL, password);
  return NULL;
}

// Public function.  Docs in header.
//
// Returns the most recent tally state provided by the tally module.
tallyStateType obs_getTallyState(void) {
  return atomic_load(&gCurrentOBSTallyState);
}

bool isMonitoredScene(const char *sceneName, char **monitoredSources, int numberOfSources) {
  for (int i = 0; i < numberOfSources; i++) {
    if (!strcmp(sceneName, monitoredSources[i])) {
      return true;
    }
  }
  return false;
}

/**
 * Called by the OBS tally support library when a new scene appears on
 * the program bus.  If the scene name matches a monitored scene, this
 * sets the tally state to red.
 */
void addSceneOnProgram(const char *sceneName) {
  if (isMonitoredScene(sceneName, gTallySourceNames, gNumberOfTallySourceNames)) {
    addActiveSceneWithName(sceneName, kTallyStateRed);
    if (obs_tally_debug) {
      fprintf(stderr, "Program source %s is a monitored source\n",
              sceneName);
    }
    updateTallyState();
  } else if (obs_tally_debug) {
    fprintf(stderr, "Program source %s is not a monitored source\n",
            sceneName);
  }
}

/**
 * Called by the OBS tally support library when a new scene appears on
 * the preview bus.  If that scene is also on the program bus and its
 * name matches a monitored scene, this function leaves the tally state
 * alone (because the program bus has priority).  Otherwise, it sets the
 * tally state to green if the scene name matches a monitored scene.
 */
void addSceneOnPreview(const char *sceneName, bool alsoOnProgram) {
  if (isMonitoredScene(sceneName, gTallySourceNames, gNumberOfTallySourceNames)) {
    addActiveSceneWithName(sceneName, alsoOnProgram ? kTallyStateRed : kTallyStateGreen);
    if (obs_tally_debug) {
      fprintf(stderr, "Preview source %s is a monitored source.\n",
              sceneName);
    }
    updateTallyState();
  } else if (obs_tally_debug) {
    fprintf(stderr, "Preview source %s is not a monitored source.\n",
            sceneName);
  }
}

/**
 * Called by the OBS tally support library when a new scene ceases to
 * be on either the program or preview bus.  If the scene name matches
 * a monitored scene, the tally state is set to off.
 */
void markSceneInactive(const char *sceneName) {
  if (isMonitoredScene(sceneName, gTallySourceNames, gNumberOfTallySourceNames)) {
    if (obs_tally_debug) {
      fprintf(stderr, "Inactive source %s is a monitored source.\n",
              sceneName);
    }
    removeActiveSceneWithName(sceneName);
    updateTallyState();
  } else if (obs_tally_debug) {
    fprintf(stderr, "Inactive source %s is not a monitored source.\n",
            sceneName);
  }
}

void runOBSTests(void) {
    fprintf(stderr, "Running OBS tests.\n");

    char *testString = "Foo|Bar|Baz";
    char **resultArray = NULL;
    int count = 0;
    splitSourceNames(testString, &resultArray, &count);

    assert(count == 3);
    assert(!strcmp(resultArray[0], "Foo"));
    assert(!strcmp(resultArray[1], "Bar"));
    assert(!strcmp(resultArray[2], "Baz"));

    assert(isMonitoredScene("Foo", resultArray, count));
    assert(isMonitoredScene("Bar", resultArray, count));
    assert(isMonitoredScene("Baz", resultArray, count));
    assert(!isMonitoredScene("Bat", resultArray, count));

    for (int i = 0; i < count; i++) {
      free((void *)resultArray[i]);
    }
    free((void *)resultArray);

    fprintf(stderr, "Done.\n");
}

void addActiveSceneWithName(const char *name, tallyStateType tallyState) {
  removeActiveSceneWithName(name);
  tally_scene_item_t *item = malloc(sizeof(*item));
  asprintf(&item->scene_name, "%s", name);
  item->tallyState = tallyState;
  item->next = activeScenes;
  activeScenes = item;
}

void removeActiveSceneWithName(const char *name) {
  tally_scene_item_t *last = NULL;
  for (tally_scene_item_t *item = activeScenes ; item ; item = item->next) {
    if (!strcmp(item->scene_name, name)) {
      if (last) {
        last->next = item->next;
      } else {
        activeScenes = item->next;
      }
      free(item->scene_name);
      free(item);
      return;
    }
    last = item;
  }
}

static void updateTallyState(void) {
  tallyStateType maxState = kTallyStateOff;
  for (tally_scene_item_t *item = activeScenes ; item ; item = item->next) {
    if (maxState < kTallyStateRed) {
      // Tally light is off.  Just use the value (promote to anything or not).
      maxState = item->tallyState;

    } else if (maxState == kTallyStateGreen) {
      // Tally light is green for preview.  Upgrade to red if needed, else ignore.
      if (item->tallyState == kTallyStateRed) {
        maxState = kTallyStateRed;
      }
    }
  }
  atomic_store(&gCurrentOBSTallyState, maxState);
  if (maxState == kTallyStateRed) {
    setTallyRed();
  } else if (maxState == kTallyStateGreen) {
    setTallyGreen();
  } else {
    setTallyOff();
  }
}



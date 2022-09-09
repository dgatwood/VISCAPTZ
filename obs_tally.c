#include <assert.h>
// Install libgettally from here: https://github.com/dgatwood/v8-libwebsocket-obs-websocket
#include <gettally.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configurator.h"
#include "constants.h"
#include "main.h"
#include "obs_tally.h"
#include "panasonicptz.h"  // SET_TALLY_STATE

void runOBSTests(void);

#if USE_OBS_TALLY_SOURCE

  static char kSourceSeparator = '|';

  static bool obs_tally_debug = true;
  static pthread_t obs_tally_thread;
  static tallyState gCurrentOBSTallyState = kTallyStateOff;
  static char **gTallySourceNames = NULL;
  static int gNumberOfTallySourceNames = 0;

  void *runOBSTallyThread(void *argIgnored);
  void addSceneOnProgram(const char *sceneName);
  void addSceneOnPreview(const char *sceneName, bool alsoOnProgram);
  void markSceneInactive(const char *sceneName);
  void splitSourceNames(char *sourceNameString, char ***outArray, int *outCount);

#endif

// Public function.  Docs in header.
//
// Initializes the OBS tally module.
bool obsModuleInit(void) {
  #if USE_OBS_TALLY_SOURCE

    char *tallySourceName = NULL;
    char *OBSWebSocketURL = getConfigKey(kOBSWebSocketURLKey);
    char *password = getConfigKey(kOBSPasswordKey) ?: "";
    tallySourceName = getConfigKey(kTallySourceName);

    setTallyOff();

    runOBSTests();

    if (OBSWebSocketURL == NULL) {
      fprintf(stderr, "No OBS web socket URL specified.  Initialization failed.\n");
      fprintf(stderr, "Run 'viscaptz --setobsurl <URL>' to fix.\n");
      return false;
    }

    if (tallySourceName == NULL) {
      fprintf(stderr, "No tally source name specified.  Initialization failed.\n");
      fprintf(stderr, "Run 'viscaptz --settallysourcename <URL>' to fix.\n");
      return false;
    }

    if (password == NULL) {
      fprintf(stderr, "WARNING: No OBS web socket password specified.\n");
      fprintf(stderr, "Run 'viscaptz --setobspass <password>' to fix.\n");
    }

    splitSourceNames(tallySourceName, &gTallySourceNames, &gNumberOfTallySourceNames);

    pthread_create(&obs_tally_thread, NULL, runOBSTallyThread, NULL);
  #endif
  return true;
}

#if USE_OBS_TALLY_SOURCE

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

    registerOBSProgramCallback(&addSceneOnProgram);
    registerOBSPreviewCallback(&addSceneOnPreview);
    registerOBSInactiveCallback(&markSceneInactive);

    // Function never returns.
    runOBSTally(OBSWebSocketURL, password);
    return NULL;
  }

  // Public function.  Docs in header.
  //
  // Returns the most recent tally state provided by the tally module.
  tallyState obs_getTallyState(void) {
    return gCurrentOBSTallyState;
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
      gCurrentOBSTallyState = kTallyStateRed;
      setTallyRed();
      if (obs_tally_debug) {
        fprintf(stderr, "Program source %s is a monitored source\n",
                sceneName);
      }
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
      if (alsoOnProgram) {
        fprintf(stderr, "Ignored preview source %s because it is on program.\n",
                sceneName);
        return;
      }
      gCurrentOBSTallyState = kTallyStateGreen;
      setTallyGreen();
      if (obs_tally_debug) {
        fprintf(stderr, "Preview source %s is a monitored source.\n",
                sceneName);
      }
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
      gCurrentOBSTallyState = kTallyStateOff;
      setTallyOff();
      if (obs_tally_debug) {
        fprintf(stderr, "Inactive source %s is a monitored source.\n",
                sceneName);
      }
    } else if (obs_tally_debug) {
      fprintf(stderr, "Inactive source %s is not a monitored source.\n",
              sceneName);
    }
  }
#endif

void runOBSTests(void) {

  #if USE_OBS_TALLY_SOURCE

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

  #endif
}

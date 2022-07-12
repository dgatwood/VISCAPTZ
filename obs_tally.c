// Install libgettally from here: https://github.com/dgatwood/v8-libwebsocket-obs-websocket
#include <gettally.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "configurator.h"
#include "constants.h"
#include "main.h"
#include "obs_tally.h"
#include "panasonicptz.h"  // SET_TALLY_STATE

bool obs_tally_debug = true;


#if USE_OBS_TALLY_SOURCE

  static pthread_t obs_tally_thread;
  static tallyState gCurrentOBSTallyState = kTallyStateOff;

  void *runOBSTallyThread(void *argIgnored);
  void addSceneOnProgram(const char *sceneName);
  void addSceneOnPreview(const char *sceneName, bool alsoOnProgram);
  void markSceneInactive(const char *sceneName);

#endif

bool obsModuleInit(void) {
  #if USE_OBS_TALLY_SOURCE

    char *OBSWebSocketURL = getConfigKey(kOBSWebSocketURLKey);
    char *password = getConfigKey(kOBSPasswordKey) ?: "";

    setTallyOff();

    if (OBSWebSocketURL == NULL) {
      fprintf(stderr, "No OBS web socket URL specified.  Initialization failed.\n");
      fprintf(stderr, "Run 'viscaptz --setobsurl <URL>' to fix.\n");
      return false;
    }

    if (password == NULL) {
      fprintf(stderr, "WARNING: No OBS web socket password specified.\n");
      fprintf(stderr, "Run 'viscaptz --setobspass <password>' to fix.\n");
    }

    pthread_create(&obs_tally_thread, NULL, runOBSTallyThread, NULL);
  #endif
  return true;
}

#if USE_OBS_TALLY_SOURCE

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

  tallyState obs_getTallyState(void) {
    return gCurrentOBSTallyState;
  }

  void addSceneOnProgram(const char *sceneName) {
    if (!strcmp(sceneName, TALLY_SOURCE_NAME)) {
      gCurrentOBSTallyState = kTallyStateRed;
      setTallyRed();
      if (obs_tally_debug) {
        fprintf(stderr, "Program source %s matches expected source %s\n",
                sceneName, TALLY_SOURCE_NAME);
      }
    } else if (obs_tally_debug) {
      fprintf(stderr, "Program source %s does not match expected source %s\n",
              sceneName, TALLY_SOURCE_NAME);
    }
  }

  void addSceneOnPreview(const char *sceneName, bool alsoOnProgram) {
    if (!strcmp(sceneName, TALLY_SOURCE_NAME)) {
      if (alsoOnProgram) {
        fprintf(stderr, "Ignored preview source %s because it is on program.\n",
                sceneName);
        return;
      }
      gCurrentOBSTallyState = kTallyStateGreen;
      setTallyGreen();
      if (obs_tally_debug) {
        fprintf(stderr, "Preview source %s matches expected source %s\n",
                sceneName, TALLY_SOURCE_NAME);
      }
    } else if (obs_tally_debug) {
      fprintf(stderr, "Preview source %s does not match expected source %s\n",
              sceneName, TALLY_SOURCE_NAME);
    }
  }

  void markSceneInactive(const char *sceneName) {
    if (!strcmp(sceneName, TALLY_SOURCE_NAME)) {
      gCurrentOBSTallyState = kTallyStateOff;
      setTallyOff();
      if (obs_tally_debug) {
        fprintf(stderr, "Inactive source %s matches expected source %s\n",
                sceneName, TALLY_SOURCE_NAME);
      }
    } else if (obs_tally_debug) {
      fprintf(stderr, "Inactive source %s does not match expected source %s\n",
              sceneName, TALLY_SOURCE_NAME);
    }
  }
#endif

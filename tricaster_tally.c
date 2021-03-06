#include "tricaster_tally.h"
#include "configurator.h"
#include "constants.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Retry every 5 seconds.
#define RECONNECT_DELAY 5000000

#if USE_TRICASTER_TALLY_SOURCE
static pthread_t tricaster_tally_thread;
static char *tally_source_name = NULL;
#endif

static tallyState gTricasterTallyState;

void handleResponses(int sock);
void handleResponse(char *buf, ssize_t length);
void handleTag(char *tag);
void *runTricasterTallyThread(void *argIgnored);
void teardown(int sock);

// Public function.  Docs in header.
//
// Initializes the Tricaster tally module.
bool tricasterModuleInit(void) {
  #if USE_TRICASTER_TALLY_SOURCE
    tally_source_name = getConfigKey(kTallySourceName);

    if (getConfigKey(kTricasterIPKey) == NULL) {
      fprintf(stderr, "Tricaster IP not set.  Initialization failed.\n");
      fprintf(stderr, "Run 'viscaptz --settricasterip <IP address>' to fix.\n");
      return false;
    }

    if (tally_source_name == NULL) {
      fprintf(stderr, "No tally source name specified.  Initialization failed.\n");
      fprintf(stderr, "Run 'viscaptz --settallysourcename <URL>' to fix.\n");
      return false;
    }

    pthread_create(&tricaster_tally_thread, NULL, runTricasterTallyThread, NULL);
  #endif

  return true;
}

// Public function.  Docs in header.
//
// Returns the most recently retrieved tally light state.
tallyState tricaster_getTallyState(void) {
  return gTricasterTallyState;
}

#if USE_TRICASTER_TALLY_SOURCE

  /** The main loop of the Tricaster tally light monitoring thread. */
  void *runTricasterTallyThread(void *argIgnored) {
    // Connect socket to TALLY_IP port 5951
    while (true) {
      // Connect to TCP port 5951.
      unsigned short port = 5951;

      struct sockaddr_in address;

      int sock = socket(AF_INET, SOCK_DGRAM, 0);
      if (sock < 0) {
        perror("socket");
        break;
      }

      memset((char *) &address, 0, sizeof(address));
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = inet_addr(getConfigKey(kTricasterIPKey));
      address.sin_port = htons(port);

      if (connect(sock, (struct sockaddr *)&address, sizeof(address)) != 0) {
          fprintf(stderr, "tricaster_tally: Tricaster connection failed.  Sleeping a bit.\n");
          usleep(RECONNECT_DELAY);
	  continue;
      }
      handleResponses(sock);
      teardown(sock);
    }
    return NULL;
  }

  /**
   * Handles source tally change messages on a Tricaster socket continuously until the
   * connection fails.
   */
  void handleResponses(int sock) {
    int flags = fcntl(sock, F_GETFL);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    char *command = "<register name=\"NTK_states\"/>\n";
    if (write(sock, command, strlen(command)) != strlen(command)) {
      fprintf(stderr, "tricaster_tally: Could not write initial request to enable callbacks.\n");
      return;
    }
    while (true) {
  #define TRICASTER_BUF_SIZE 4096
      char buf[TRICASTER_BUF_SIZE + 1];

      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(sock, &read_fds);

      if (select(sock + 1, &read_fds, NULL, NULL, NULL) > 0) {
        ssize_t length = read(sock, buf, TRICASTER_BUF_SIZE);
        if (length == -1) {
          fprintf(stderr, "tricaster_tally: Could not read from socket.\n");
          return;
        }
        buf[length] = '\0';
        handleResponse(buf, length);
      }
    }
  }

  // Parse XML results:
  // <shortcut_states>
  // <shortcut_state name="program_tally" value="INPUT1|BFR2|DDR3" type="" sender="" />
  // <shortcut_state name="preview_tally" value="INPUT7" type="" sender="" />
  // </shortcut_states>
  /** Handles a single tally data response from Tricaster. */
  void handleResponse(char *buf, ssize_t length) {
    char *pos = buf;

    while ((pos = strstr(pos, "<shortcut_state ")) != NULL) {
      char *end = strstr(pos, ">");
      if (end == NULL) {
        // Ick.  Something went wrong.
        fprintf(stderr, "tricaster_tally: WARNING: Got partial buffer.\n");
        handleTag(pos);
        break;
      }
      ssize_t length = end - pos + 1;
      char *tag = malloc(length + 1);
      strncpy(tag, pos, length);
      tag[length] = '\0';
      handleTag(tag);
      free(tag);
      pos = end + 1;
    }
  }

  /** Posts tally state changes to the camera, if applicable, and updates the internal state. */
  void setTallyStateFromTricaster(tallyState newState) {
    gTricasterTallyState = newState;
    #ifdef SET_TALLY_STATE
      SET_TALLY_STATE(gTricasterTallyState);
    #endif
  }


  // <shortcut_state name="program_tally" value="INPUT1|BFR2|DDR3" type="" sender="" />
  // <shortcut_state name="preview_tally" value="INPUT7" type="" sender="" />
  /** Processes a single XML tag looking for tally information. */
  void handleTag(char *tag) {
    bool setProgram = (strstr(tag, "name=\"program_tally\"") != NULL);
    bool setPreview = (strstr(tag, "name=\"preview_tally\"") != NULL);
    if (!setProgram && !setPreview) {
      return;
    }

    char *value = strstr(tag, "value=\"");
    if (value == NULL) {
      return;
    }
    value += 6;

    // Watch for tally_source_name.
    ssize_t length = strlen(tally_source_name);

    bool found = false;
    if (!strncmp(value, tally_source_name, length)) {
      if (value[length] == '|' || value[length] == '"') {
        found = true;
      }
    }
    if (!found) {
      while ((value = strstr(value, "|")) != NULL) {
        value += 1;
        if (!strncmp(value, tally_source_name, length)) {
          if (value[length] == '|' || value[length] == '"') {
            found = true;
            break;
          }
        }
      }
    }

    if (found == true) {
      if (setProgram) {
        setTallyStateFromTricaster(kTallyStateRed);
      } else {
        setTallyStateFromTricaster(kTallyStateGreen);
      }
    } else {
      setTallyStateFromTricaster(kTallyStateOff);
    }
  }

  /** Unregisters the active tally state change subscription and closes the Tricaster socket. */
  void teardown(int sock) {
    const char *endCommand = "<unregister name=\"NTK_states\"/>\n";
    write(sock, endCommand, strlen(endCommand));
    close(sock);
  }

#endif

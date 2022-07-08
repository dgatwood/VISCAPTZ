#include "tricaster_tally.h"
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

static pthread_t tricaster_tally_thread;

static tallyState gTricasterTallyState;

void handleResponses(int sock);
void handleResponse(char *buf, ssize_t length);
void handleTag(char *tag);
void *runTricasterTallyThread(void *argIgnored);
void teardown(int sock);

bool tricasterModuleInit(void) {
  pthread_create(&tricaster_tally_thread, NULL, runTricasterTallyThread, NULL);
  return true;
}

tallyState tricaster_getTallyState(void) {
  return kTallyStateOff;
}

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
    address.sin_addr.s_addr = inet_addr(TALLY_IP);
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

// Returns when anything goes wrong.
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

void handleResponse(char *buf, ssize_t length) {
  char *pos = buf;

  while ((pos = strstr(pos, "<shortcut_state ")) != NULL) {
    char *end = strstr(pos, ">");
    if (end == NULL) {
      // Ick.
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


// <shortcut_state name="program_tally" value="INPUT1|BFR2|DDR3" type="" sender="" />
// <shortcut_state name="preview_tally" value="INPUT7" type="" sender="" />
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

  // Watch for TALLY_SOURCE_NAME.
  ssize_t length = strlen(TALLY_SOURCE_NAME);

  bool found = false;
  if (!strncmp(value, TALLY_SOURCE_NAME, length)) {
    if (value[length] == '|' || value[length] == '"') {
      found = true;
    }
  }
  if (!found) {
    while ((value = strstr(value, "|")) != NULL) {
      value += 1;
      if (!strncmp(value, TALLY_SOURCE_NAME, length)) {
        if (value[length] == '|' || value[length] == '"') {
          found = true;
          break;
        }
      }
    }
  }

  if (found == true) {
    if (setProgram) {
      gTricasterTallyState = kTallyStateRed;
    } else {
      gTricasterTallyState = kTallyStateGreen;
    }
  } else {
    gTricasterTallyState = kTallyStateOff;
  }
}

void teardown(int sock) {
  const char *endCommand = "<unregister name=\"NTK_states\"/>\n";
  write(sock, endCommand, strlen(endCommand));
  close(sock);
}

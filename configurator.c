#define _GNU_SOURCE  // For asprintf, because Linux is weird.

#define ENABLE_CONFIGURATOR_DEBUGGING 0

#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "configurator.h"

static bool configuratorDebug = false;

bool lineMatchesKey(const char *buf, const char *key) {
  size_t keyLength = strlen(key);
  return (strlen(buf) > (keyLength + 1)) &&
      !strncmp(key, buf, keyLength) &&
      (buf[keyLength] == '=');
}

int64_t getConfigKeyInteger(const char *key) {
  bool localDebug = configuratorDebug || false;
  char *stringValue = getConfigKey(key);
  if (stringValue == NULL) {
    if (localDebug) {
      fprintf(stderr, "Request for non-existent key %s as integer.  Returning zero (0).\n", key);
    }
    return 0;
  }
  if (localDebug) {
    fprintf(stderr, "String value: %s\n", stringValue);
  }
  uint64_t intValue = strtoull(stringValue, NULL, 10);
  free(stringValue);
  return intValue;
}

bool getConfigKeyBool(const char *key) {
  char *stringValue = getConfigKey(key);
  if (stringValue == NULL) {
    if (configuratorDebug) {
      fprintf(stderr, "Request for non-existent key %s as bool.  Returning false.\n", key);
    }
    return false;
  }
  bool boolValue = !strcmp(stringValue, "1");
  free(stringValue);
  return boolValue;
}

char *getConfigKey(const char *key) {
  bool localDebug = configuratorDebug || false;

  FILE *fp = fopen(CONFIG_FILE_PATH, "r");
  if (localDebug && fp == NULL) {
    perror("viscaptz");
    fprintf(stderr, "Could not open file \"%s\" for reading\n", CONFIG_FILE_PATH);
  }
  char *buf = NULL;
  size_t linecapacity = 0;
  while (fp != NULL) {
    ssize_t length = getline(&buf, &linecapacity, fp);
    if (localDebug) {
      fprintf(stderr, "Got line %s\n", buf);
    }
    if (length == -1) break;
    if (lineMatchesKey(buf, key)) {
      if (localDebug) {
        fprintf(stderr, "MATCHES KEY %s\n", key);
      }
      char *retval = NULL;
      asprintf(&retval, "%s", &buf[strlen(key) + 1]);
      retval[strlen(retval)-1] = '\0';  // Remove terminating newline
      if (localDebug) {
        fprintf(stderr, "Will return \"%s\"\n", retval);
      }
      free(buf);
#if ENABLE_CONFIGURATOR_DEBUGGING
      fprintf(stderr, "Key %s value %s\n", key, retval);
#endif
      fclose(fp);
      return retval;
    } else {
      if (localDebug) {
        fprintf(stderr, "DOES NOT MATCH KEY %s\n", key);
      }
    }
  }
  free(buf);
  if (fp) {
    fclose(fp);
  }
  return NULL;
}

bool setConfigKey(const char *key, const char *value) {
  FILE *fp = fopen(CONFIG_FILE_PATH, "r");
  if (!fp) {
    fprintf(stderr, "WARNING: Could not open %s for reading",
            CONFIG_FILE_PATH);
  }

#ifdef __linux__
  // Linux dirname, by default, is broken, and modifies its buffer.  (Why!?!)
  char *junk = NULL;
  asprintf(&junk, "%s", CONFIG_FILE_PATH);
  char *directory = dirname(junk);
#else
  char *directory = dirname(CONFIG_FILE_PATH);
#endif

  char *tempfilename = tempnam(directory, "viscaptz-temp-");
  char *buf = NULL;
  size_t linecapacity = 0;
  FILE *fq = fopen(tempfilename, "w");
  if (!fq) {
    free(tempfilename);
    if (fp) {
      fclose(fp);
    }
#ifdef __linux__
    free(junk);
#endif
    return false;
  }

  bool found = false, error = false;
  while (fp != NULL) {
    ssize_t length = getline(&buf, &linecapacity, fp);
    if (length == -1) break;
    if (lineMatchesKey(buf, key)) {
      // Matching line.
      if (value != NULL) {
        error = error || (fprintf(fq, "%s=%s\n", key, value) == -1);
      }
      found = true;
    } else {
      error = error || (fprintf(fq, "%s", buf) == -1);
    }
  }
  if (!found) {
    error = error || (fprintf(fq, "%s=%s\n", key, value) == -1);
  }

  if (fp) {
    fclose(fp);
  }
  fclose(fq);

  if (!error) {
    error = error || (rename(tempfilename, CONFIG_FILE_PATH) != 0);
  }
  free(tempfilename);
  free(buf);
#ifdef __linux__
  free(junk);
#endif
  return !error;
}

bool removeConfigKey(const char *key) {
  return setConfigKey(key, NULL);
}

bool setConfigKeyBool(const char *key, bool value) {
  return setConfigKey(key, value ? "1" : "0");
}

bool setConfigKeyInteger(const char *key, int64_t value) {
  bool localDebug = configuratorDebug || false;
  char *stringValue = NULL;
  asprintf(&stringValue, "%" PRId64, value);

  if (localDebug) {
    fprintf(stderr, "Writing %s -> %s ( == %" PRId64 ")\n", key, stringValue, value);
  }
  bool retval = setConfigKey(key, stringValue);

  free(stringValue);
  return retval;
}

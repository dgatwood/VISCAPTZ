#define _GNU_SOURCE  // For asprintf, because Linux is weird.

#define ENABLE_CONFIGURATOR_DEBUGGING 0

#include <libgen.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "constants.h"
#include "configurator.h"

static bool configuratorDebug = false;

// Returns true if the specified line's key portion matches the specified key.
bool lineMatchesKey(const char *buf, const char *key) {
  size_t keyLength = strlen(key);
  return (strlen(buf) > (keyLength + 1)) &&
      !strncmp(key, buf, keyLength) &&
      (buf[keyLength] == '=');
}

// Returns the value of the specified key as a 64-bit signed integer value.
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

// Returns the value of the specified key as a Boolean value.
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

// Returns the configuration file path (~/.viscaptz.conf unless overridden).
const char *getConfigFilePath(void) {
  #ifdef CONFIG_FILE_PATH
    return CONFIG_FILE_PATH;
  #endif

  static char *value = NULL;

  if (value != NULL) {
    return value;
  }

  struct passwd *pw = getpwuid(getuid());

  asprintf(&value, "%s/.viscaptz.conf", pw->pw_dir);
  return value;
}

// Returns the value of the specified key as a string.
char *getConfigKey(const char *key) {
  bool localDebug = configuratorDebug || false;

  FILE *fp = fopen(getConfigFilePath(), "r");
  if (localDebug && fp == NULL) {
    perror("viscaptz");
    fprintf(stderr, "Could not open file \"%s\" for reading\n", getConfigFilePath());
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

// Sets the configuration key to the specified string value.
bool setConfigKey(const char *key, const char *value) {
  FILE *fp = fopen(getConfigFilePath(), "r");
  if (!fp) {
    fprintf(stderr, "WARNING: Could not open %s for reading",
            getConfigFilePath());
  }

  // Some dirname implementations modify their buffer.  Be safe.
  char *configFilePathCopy = NULL;
  asprintf(&configFilePathCopy, "%s", getConfigFilePath());
  char *directory = dirname(configFilePathCopy);

  char *tempfilename = tempnam(directory, "viscaptz-temp-");
  char *buf = NULL;
  size_t linecapacity = 0;
  FILE *fq = fopen(tempfilename, "w");
  if (!fq) {
    free(tempfilename);
    if (fp) {
      fclose(fp);
    }

    free(configFilePathCopy);
    return false;
  }

  bool found = false, error = false;
  while (fp != NULL) {
    ssize_t length = getline(&buf, &linecapacity, fp);
    if (length == -1) break;
    if (lineMatchesKey(buf, key)) {
      // Matching line.
      if (value != NULL) {
        // Write the new value if it is non-NULL, else treat it as a deletion.
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
    error = error || (rename(tempfilename, getConfigFilePath()) != 0);
  }
  free(tempfilename);
  free(buf);
  free(configFilePathCopy);
  return !error;
}

// Deletes the specified configuration key.
bool removeConfigKey(const char *key) {
  return setConfigKey(key, NULL);
}

// Sets the configuration key to the specified Boolean value.
bool setConfigKeyBool(const char *key, bool value) {
  return setConfigKey(key, value ? "1" : "0");
}

// Sets the configuration key to the specified 64-bit signed integer value.
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

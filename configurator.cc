#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // For asprintf, because Linux is weird.
#endif

#define ENABLE_CONFIGURATOR_DEBUGGING 0

#include <libgen.h>
#include <pthread.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <unordered_map>

#include "constants.h"
#include "configurator.h"

static bool configuratorDebug = false;
static pthread_mutex_t configFileMutex = PTHREAD_MUTEX_INITIALIZER;
static std::unordered_map<std::string, std::string> configMap;
static bool configLoaded = false;

static const char *getConfigFilePath(void);

// Returns true if the specified line's key portion matches the specified key.
static bool lineMatchesKey(const char *buf, const char *key) {
  size_t keyLength = strlen(key);
  return (strlen(buf) >= (keyLength + 1)) &&
      !strncmp(key, buf, keyLength) &&
      (buf[keyLength] == '=');
}

static void loadConfigurationIfNeeded(void) {
  if (configLoaded) {
    return;
  }
  configLoaded = true;

  bool localDebug = configuratorDebug || false;
  const char *configFilePath = getConfigFilePath();
  FILE *fp = fopen(configFilePath, "r");
  if (fp == NULL) {
    if (localDebug) {
      perror("viscaptz");
      fprintf(stderr, "Could not open file \"%s\" for reading\n", configFilePath);
    }
    return;
  }

  char *buf = NULL;
  size_t linecapacity = 0;
  while (true) {
    ssize_t length = getline(&buf, &linecapacity, fp);
    if (localDebug) {
      fprintf(stderr, "Got line %s\n", buf ?: "<EOF>");
    }
    if (length == -1) break;

    while (length > 0 && (buf[length - 1] == '\r' || buf[length - 1] == '\n')) {
      buf[--length] = '\0';
    }
    char *equalsPos = strchr(buf, '=');
    if (equalsPos != NULL && equalsPos != buf) {
      *equalsPos = '\0';
      configMap[buf] = equalsPos + 1;
    }
  }
  free(buf);
  fclose(fp);
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
  int64_t intValue = strtoll(stringValue, NULL, 10);
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
// Must be called with configFileMutex held.
static const char *getConfigFilePath(void) {
  #ifdef CONFIG_FILE_PATH
    return CONFIG_FILE_PATH;
  #else
    static char *value = NULL;

    if (value != NULL) {
      return value;
    }

    struct passwd *pw = getpwuid(getuid());

    const char *basepath = (pw != NULL && pw->pw_dir != NULL) ? pw->pw_dir : getenv("HOME");
    asprintf(&value, "%s/.viscaptz.conf", (basepath ?: "."));

    return value;
  #endif
}

// Returns the value of the specified key as a string.
char *getConfigKey(const char *key) {
  bool localDebug = configuratorDebug || false;

  pthread_mutex_lock(&configFileMutex);
  loadConfigurationIfNeeded();

  auto it = configMap.find(key);
  if (it != configMap.end()) {
    char *retval = strdup(it->second.c_str());
    if (localDebug) {
      fprintf(stderr, "MATCHES KEY %s, will return \"%s\"\n", key, retval);
    }
#if ENABLE_CONFIGURATOR_DEBUGGING
    fprintf(stderr, "Key %s value %s\n", key, retval);
#endif
    pthread_mutex_unlock(&configFileMutex);
    return retval;
  }

  if (localDebug) {
    fprintf(stderr, "DOES NOT MATCH KEY %s\n", key);
  }
  pthread_mutex_unlock(&configFileMutex);
  return NULL;
}

// Sets the configuration key to the specified string value.
bool setConfigKey(const char *key, const char *value) {
  pthread_mutex_lock(&configFileMutex);
  loadConfigurationIfNeeded();

  const char *configFilePath = getConfigFilePath();
  FILE *fp = fopen(configFilePath, "r");
  if (!fp) {
    fprintf(stderr, "WARNING: Could not open %s for reading",
            configFilePath);
  }

  // Some dirname implementations modify their buffer.  Be safe.
  char *configFilePathCopy = NULL;
  asprintf(&configFilePathCopy, "%s", configFilePath);
  char *directory = dirname(configFilePathCopy);

  char *tempfilename = NULL;
  asprintf(&tempfilename, "%s/.viscaptz-temp-XXXXXX", directory);
  int tempfd = mkstemp(tempfilename);
  char *buf = NULL;
  size_t linecapacity = 0;
  FILE *fq = (tempfd != -1) ? fdopen(tempfd, "w") : NULL;
  if (!fq) {
    if (tempfd != -1) {
      close(tempfd);
      unlink(tempfilename);
    }
    if (fp) {
      fclose(fp);
    }

    free(configFilePathCopy);
    fprintf(stderr, "Could not open file %s for writing.\n", tempfilename);
    free(tempfilename);
    pthread_mutex_unlock(&configFileMutex);
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
        bool newError = (fprintf(fq, "%s=%s\n", key, value) < 0);
        error = error || newError;
        if (newError) {
          fprintf(stderr, "Error rewriting key line.\n");
        }
      }
      found = true;
    } else {
      bool needsNewline = (length > 0 && buf[length - 1] != '\n');
      bool newError = (fprintf(fq, "%s%s", buf, needsNewline ? "\n" : "") < 0);
      error = error || newError;
      if (error) {
        fprintf(stderr, "Error writing non-key line.\n");
      }
    }
  }
  if ((!found) && value != NULL) {
    bool newError = (fprintf(fq, "%s=%s\n", key, value) < 0);
    error = error || newError;
    if (error) {
      fprintf(stderr, "Error writing key line.\n");
    }
  }

  if (fp) {
    fclose(fp);
  }
  bool newError = (fclose(fq) != 0);
  error = error || newError;

  if (!error) {
    newError = (rename(tempfilename, configFilePath) != 0);
    error = error || newError;
    if (error) {
      fprintf(stderr, "Error renaming temp file.\n");
      unlink(tempfilename);
    } else {
      if (value == NULL) {
        configMap.erase(key);
      } else {
        configMap[key] = value;
      }
    }
  } else {
    unlink(tempfilename);
  }
  free(tempfilename);
  free(buf);
  free(configFilePathCopy);
  pthread_mutex_unlock(&configFileMutex);
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

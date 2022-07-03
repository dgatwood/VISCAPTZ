#include <inttypes.h>
#include <stdbool.h>

/**
 * Returns the value of the specified configuration key (or NULL).
 * Values must be freed by the caller.
 */
char *getConfigKey(const char *key);

/**
 * Returns the value of the specified configuration key as a boolean.
 * Missing values are returned as false.
 */
bool getConfigKeyBool(const char *key);

/**
 * Returns the value of the specified configuration key as a 64-bit
 * integer value.  Missing values are returned as zero (0).
 */
int64_t getConfigKeyInteger(const char *key);

/**
 * Sets the value of the specified configuration key.
 * Keys must not contain equals signs.
 * Values must not contain newlines.
 */
bool setConfigKey(const char *key, const char *value);

/** Sets the specified configuration key to 0/1. */
bool setConfigKeyBool(const char *key, bool value);

/** Sets the specified configuration key to 0/1. */
bool setConfigKeyInteger(const char *key, int64_t value);

/** Removes the value for the specified configuration key. */
bool removeConfigKey(const char *key);

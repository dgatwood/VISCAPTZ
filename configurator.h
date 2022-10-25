#include <inttypes.h>
#include <stdbool.h>

/**
 * Returns the value of the specified configuration key (or NULL).
 * Values must be freed by the caller.
 */
char *getConfigKey(const char *key);

/**
 * Returns the value of the specified configuration key as a Boolean
 * value.  Missing values are returned as false.
 */
bool getConfigKeyBool(const char *key);

/**
 * Returns the value of the specified configuration key as a 64-bit
 * signed integer value.  Missing values are returned as zero (0).
 */
int64_t getConfigKeyInteger(const char *key);

/**
 * Sets the value of the specified configuration key.
 *
 * IMPORTANT: These functions perform NO synchronization.  Although
 * writes are atomic against reads, they are NOT atomic against
 * other writes.
 *
 * These functions should ONLY be called during calibration or while
 * handling command-line arguments at launch time, to ensure that
 * concurrent writes from multiple threads cannot happen.
 *
 * @property key   The name of the key.  This key must not contain
 *                 any equals signs.
 * @property value The vale for the key.  This value must not
 *                 contain any newline characters.  If the value
 *                 is NULL, the key is removed.
 */
bool setConfigKey(const char *key, const char *value);

/**
 * Sets the specified configuration key to 0/1.
 * See setConfigKey for more details.
 */
bool setConfigKeyBool(const char *key, bool value);

/**
 * Sets the specified configuration key to 0/1.
 * See setConfigKey for more details.
 */
bool setConfigKeyInteger(const char *key, int64_t value);

/**
 * Removes the value for the specified configuration key.
 * See setConfigKey for more details.
 */
bool removeConfigKey(const char *key);

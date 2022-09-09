#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"


/** Initializes the OBS tally module. */
bool obsModuleInit(void);

/** Returns the current tally state based on the active OBS scene. */
tallyState obs_getTallyState(void);

// If the OBS tally source is enabled, map the standard tally getter
// macro to a function in this module.
#if USE_OBS_TALLY_SOURCE
  #define GET_TALLY_STATE() obs_getTallyState()
#endif

#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"

bool tricasterModuleInit(void);
bool tricasterModuleStart(void);

tallyState tricaster_getTallyState(void);

// If the Tricaster tally source is enabled, map the standard tally getter
// macro to a function in this module.
#if USE_TRICASTER_TALLY_SOURCE
  #define GET_TALLY_STATE() tricaster_getTallyState()
#endif

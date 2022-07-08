#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"

bool tricasterModuleInit(void);

tallyState tricaster_getTallyState(void);

#if USE_OBS_TALLY_SOURCE
  #define GET_TALLY_STATE() tricaster_getTallyState()
#endif

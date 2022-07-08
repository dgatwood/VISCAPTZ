#include <inttypes.h>
#include <stdbool.h>

#include "constants.h"

bool obsModuleInit(void);

tallyState obs_getTallyState(void);

#if USE_OBS_TALLY_SOURCE
  #define GET_TALLY_STATE() obs_getTallyState()
#endif

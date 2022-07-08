#include "constants.h"
#include "obs_tally.h"

/**
 * Unfortunately, OBS provides no C or C++ client implementation, and does
 * everything using the WebSocket protocol, for which I haven't had much
 * luck finding a lightweight C or C++ implementation that supports arbitrary
 * JSON data, and I'd rather not bring in something as heavyweight as Node.js.
 * (Neither V8 nor JavaScriptCore provide any built-in Websocket support, and
 * I was balking at bringing in even something that big.  And I really don't
 * want to bring in all of Python for the same reason.
 *
 * So for now, this is just a stub.
 */
bool obsModuleInit(void) {
  return false;
}

tallyState obs_getTallyState(void) {
  return kTallyStateOff;
}

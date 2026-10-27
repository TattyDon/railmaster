#include "gestures.hpp"

// Without a trackpad gesture to watch (all but macOS): nothing to report.
namespace railmaster::client {

void watch_pinch() {}
double take_pinch() { return 0.0; }

} // namespace railmaster::client

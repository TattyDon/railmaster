#pragma once

namespace railmaster::client {

// A pinch on a Mac trackpad, which SDL2 does not pass on: watched for
// through the system's own events (gestures_mac.mm). Elsewhere these do
// nothing (gestures.cpp).
void watch_pinch();
// How much the trackpad has been pinched since the last call: the sum of
// the system's magnification steps (spreading the fingers is positive).
double take_pinch();

} // namespace railmaster::client

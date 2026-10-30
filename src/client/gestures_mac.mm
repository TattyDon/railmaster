#include "gestures.hpp"

#import <Cocoa/Cocoa.h>

// The trackpad's pinch, as the system reports it to the app (a magnify
// event for each step), watched for alongside SDL's own event handling.
// Called on the main thread, as SDL pumps the app's events.
namespace railmaster::client {

namespace {
double g_pinch = 0.0;
id g_monitor = nil;
} // namespace

void watch_pinch() {
    if (g_monitor != nil) return;
    g_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify
                                                      handler:^NSEvent*(NSEvent* event) {
                                                          g_pinch += static_cast<double>([event magnification]);
                                                          return event;
                                                      }];
}

double take_pinch() {
    const double pinch = g_pinch;
    g_pinch = 0.0;
    return pinch;
}

} // namespace railmaster::client

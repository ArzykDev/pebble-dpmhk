#pragma once
#include "../model.h"

// Push the departures window and request departures for the given stop
void departures_window_push(const StopRef *stop);
// Launch: push the board and let the phone pick the stop (nearest favorite
// within 300 m, else the nearest stop, else favorite #1)
void departures_window_push_auto(void);

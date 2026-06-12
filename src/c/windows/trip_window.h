#pragma once

// Push the trip-route window for a chosen departure: lists the stops it passes
// through from the current stop onward (names only — the API has no times).
void trip_window_push(const char *line, const char *dest);

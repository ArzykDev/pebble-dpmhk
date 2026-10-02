#pragma once

// Push the trip-route window for a chosen departure: lists the stops it passes
// through from the current stop onward (names only — the API has no times).
// time is that line's next "HH:MM" departure, offered as a reminder.
void trip_window_push(const char *line, const char *dest, const char *time);

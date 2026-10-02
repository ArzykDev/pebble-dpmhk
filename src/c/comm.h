#pragma once
#include <pebble.h>

// Operation codes (shared contract with pkjs)
enum {
  OP_GET_DEPARTURES = 1,
  OP_GET_NEAREST = 2,
  OP_GET_TRIP = 3,         // route stops a chosen departure passes through
  OP_FAVORITES = 4,        // push from JS (config closed / favorites changed)
  // 5/6 (watch-side favorite add/remove) are retired: favorites live in Clay
  OP_REMINDER = 7,  // watch → phone: (re)place or, with no time, drop the pin
};

typedef void (*CommUpdatedHandler)(void);

void comm_init(void);
void comm_deinit(void);

// Called whenever the departure board changed (rows arrived, error, ...)
void comm_set_board_handler(CommUpdatedHandler handler);
// Called whenever the stops model changed (nearest results, favorites push)
void comm_set_stops_handler(CommUpdatedHandler handler);
// Called whenever the trip route model changed (rows arrived, error, ...)
void comm_set_trip_handler(CommUpdatedHandler handler);

void comm_request_departures(const char *stop_id, const char *stop_name);
// Re-fetch the current board in the background: it stays on screen until the
// new one has fully arrived, and survives a failed fetch.
void comm_refresh_departures(void);

// Fire-and-forget (REQUEST_ID 0, no counter bump): ask the phone to replace
// the reminder pin with one for this departure, or remove it when hhmm is "".
// False when the message couldn't be sent.
bool comm_send_reminder(const char *line, const char *dest, const char *hhmm,
                        const char *stop_name);
void comm_request_nearest(void);
// Fetch the downstream stops of a departure (line + destination text) from the
// board's stop; the phone resolves the /trasa direction by name.
void comm_request_trip(const char *line, const char *dest);


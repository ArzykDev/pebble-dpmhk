#pragma once
#include <pebble.h>

// Operation codes (shared contract with pkjs)
enum {
  OP_GET_DEPARTURES = 1,
  OP_GET_NEAREST = 2,
  OP_GET_TRIP = 3,         // route stops a chosen departure passes through
  OP_FAVORITES = 4,        // push from JS (config closed / favorites changed)
  // 5/6 (watch-side favorite add/remove) are retired: favorites live in Clay
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
void comm_request_nearest(void);
// Fetch the downstream stops of a departure (line + destination text) from the
// given current stop; the phone resolves the /trasa direction by name.
void comm_request_trip(const char *line, const char *dest,
                       const char *stop_name);


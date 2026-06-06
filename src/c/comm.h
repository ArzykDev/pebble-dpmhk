#pragma once
#include <pebble.h>

// Operation codes (shared contract with pkjs)
enum {
  OP_GET_DEPARTURES = 1,
  OP_GET_NEAREST = 2,
  OP_FAVORITES = 4,        // push from JS (config closed / favorites changed)
  OP_ADD_FAVORITE = 5,     // watch long-press on a nearest stop
  OP_REMOVE_FAVORITE = 6,  // watch long-press on a favorite
};

typedef void (*CommUpdatedHandler)(void);

void comm_init(void);
void comm_deinit(void);

// Called whenever the departure board changed (rows arrived, error, ...)
void comm_set_board_handler(CommUpdatedHandler handler);
// Called whenever the stops model changed (nearest results, favorites push)
void comm_set_stops_handler(CommUpdatedHandler handler);

void comm_request_departures(const char *stop_id, const char *stop_name);
void comm_request_nearest(void);

// Fire-and-forget; JS answers with an OP_FAVORITES push that updates the model
void comm_add_favorite(const char *stop_id);
void comm_remove_favorite(const char *stop_id);

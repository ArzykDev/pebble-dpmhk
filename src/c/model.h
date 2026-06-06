#pragma once
#include <pebble.h>

#define MAX_DEPARTURES 18
#define MAX_FAVORITES 6
#define MAX_NEAREST 5

#define LINE_LEN 6
#define DEST_LEN 40
#define TIME_LEN 8
#define NAME_LEN 32
#define ID_LEN 8
#define DIST_LEN 8

// Sentinel for "no realtime delay known" (shared contract with pkjs)
#define DELAY_UNKNOWN -32768

// META_FLAGS bits (shared contract with pkjs)
#define BOARD_FLAG_CACHED (1 << 0)
#define BOARD_FLAG_STALE (1 << 1)

// ERROR codes (shared contract with pkjs)
enum {
  ERR_NONE = 0,
  ERR_NETWORK = 1,
  ERR_API = 2,
  ERR_PARSE = 3,
  ERR_NO_PACKET = 4,
  ERR_GPS = 5,
};

typedef struct {
  char line[LINE_LEN];  // trimmed line number, e.g. "6", "16S"
  char dest[DEST_LEN];  // destination ("smer"), UTF-8
  char time[TIME_LEN];  // departure "HH:MM"
  int32_t delay;        // seconds, DELAY_UNKNOWN if not known
} Departure;

typedef struct {
  char id[ID_LEN];      // api.dpmhk.cz stop id
  char name[NAME_LEN];  // stop name, UTF-8
  char dist[DIST_LEN];  // preformatted distance ("320 m"), "" for favorites
} StopRef;

typedef struct {
  Departure items[MAX_DEPARTURES];
  uint8_t count;     // rows received so far
  uint8_t expected;  // META_COUNT from response header
  char stop_id[ID_LEN];
  char stop_name[NAME_LEN];
  char fetched_at[TIME_LEN];  // "HH:MM" the data was fetched (for offline badge)
  uint8_t flags;              // BOARD_FLAG_*
  uint8_t error;              // ERR_*
  uint32_t request_id;
  bool loading;
} DepartureBoard;

typedef struct {
  StopRef favorites[MAX_FAVORITES];
  uint8_t favorites_count;
  StopRef nearest[MAX_NEAREST];
  uint8_t nearest_count;
  uint8_t nearest_expected;
  bool nearest_loading;
  uint8_t nearest_error;  // ERR_*
} StopsModel;

DepartureBoard *model_board(void);
StopsModel *model_stops(void);

// Reset the board for a new request (sets loading, clears rows/error)
void model_board_begin_request(uint32_t request_id, const char *stop_id,
                               const char *stop_name);

#include "model.h"

static DepartureBoard s_board;
static StopsModel s_stops;
static TripModel s_trip;

DepartureBoard *model_board(void) {
  return &s_board;
}

StopsModel *model_stops(void) {
  return &s_stops;
}

TripModel *model_trip(void) {
  return &s_trip;
}

void model_board_begin_request(uint32_t request_id, const char *stop_id,
                               const char *stop_name) {
  s_board.count = 0;
  s_board.expected = 0;
  s_board.flags = 0;
  s_board.error = ERR_NONE;
  s_board.request_id = request_id;
  s_board.loading = true;
  s_board.fetched_at[0] = '\0';
  strncpy(s_board.stop_id, stop_id, ID_LEN - 1);
  s_board.stop_id[ID_LEN - 1] = '\0';
  strncpy(s_board.stop_name, stop_name, NAME_LEN - 1);
  s_board.stop_name[NAME_LEN - 1] = '\0';
}

void model_trip_begin_request(uint32_t request_id, const char *line,
                              const char *dest) {
  s_trip.count = 0;
  s_trip.expected = 0;
  s_trip.error = ERR_NONE;
  s_trip.request_id = request_id;
  s_trip.loading = true;
  strncpy(s_trip.line, line, LINE_LEN - 1);
  s_trip.line[LINE_LEN - 1] = '\0';
  strncpy(s_trip.dest, dest, DEST_LEN - 1);
  s_trip.dest[DEST_LEN - 1] = '\0';
}

#include "model.h"

static DepartureBoard s_board;
static StopsModel s_stops;

DepartureBoard *model_board(void) {
  return &s_board;
}

StopsModel *model_stops(void) {
  return &s_stops;
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

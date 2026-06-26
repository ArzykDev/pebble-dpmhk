#include "persist.h"

#define PERSIST_KEY_FAVORITES 1
#define PERSIST_KEY_LAST_BOARD 2

#define PERSIST_BOARD_ROWS 4
#define PERSIST_DEST_LEN 28

typedef struct __attribute__((__packed__)) {
  char id[ID_LEN];
  char name[NAME_LEN];
} PersistStop;

typedef struct __attribute__((__packed__)) {
  uint8_t count;
  PersistStop stops[MAX_FAVORITES];
} PersistFavorites;  // 1 + 6*42 = 253 bytes, under PERSIST_DATA_MAX_LENGTH

void persist_load_favorites(void) {
  StopsModel *stops = model_stops();
  if (!persist_exists(PERSIST_KEY_FAVORITES)) {
    stops->favorites_count = 0;
    return;
  }
  PersistFavorites blob;
  int read = persist_read_data(PERSIST_KEY_FAVORITES, &blob, sizeof(blob));
  // Reject a malformed blob or one written by an older NAME_LEN layout (the
  // per-stop stride changed): a layout mismatch reads as a wrong byte length.
  // Dropping it clears favorites until the phone re-pushes the mirror on 'ready'.
  if (read < 1 || blob.count > MAX_FAVORITES ||
      read != (int)(1 + blob.count * sizeof(PersistStop))) {
    stops->favorites_count = 0;
    return;
  }
  stops->favorites_count = blob.count;
  for (uint8_t i = 0; i < blob.count; i++) {
    strncpy(stops->favorites[i].id, blob.stops[i].id, ID_LEN - 1);
    stops->favorites[i].id[ID_LEN - 1] = '\0';
    strncpy(stops->favorites[i].name, blob.stops[i].name, NAME_LEN - 1);
    stops->favorites[i].name[NAME_LEN - 1] = '\0';
    stops->favorites[i].dist[0] = '\0';
  }
}

void persist_store_favorites(void) {
  StopsModel *stops = model_stops();
  PersistFavorites blob;
  memset(&blob, 0, sizeof(blob));
  blob.count = stops->favorites_count;
  for (uint8_t i = 0; i < blob.count; i++) {
    strncpy(blob.stops[i].id, stops->favorites[i].id, ID_LEN - 1);
    strncpy(blob.stops[i].name, stops->favorites[i].name, NAME_LEN - 1);
  }
  persist_write_data(PERSIST_KEY_FAVORITES, &blob,
                     1 + blob.count * sizeof(PersistStop));
}

typedef struct __attribute__((__packed__)) {
  char line[LINE_LEN];
  char dest[PERSIST_DEST_LEN];
  char time[TIME_LEN];
} PersistRow;

typedef struct __attribute__((__packed__)) {
  char stop_id[ID_LEN];
  char stop_name[NAME_LEN];
  char fetched_at[TIME_LEN];
  uint8_t count;
  PersistRow rows[PERSIST_BOARD_ROWS];
} PersistBoard;  // 51 + 4*42 = 219 bytes, under PERSIST_DATA_MAX_LENGTH

void persist_store_board(void) {
  const DepartureBoard *board = model_board();
  if (board->count == 0 || board->error != ERR_NONE) {
    return;
  }
  PersistBoard blob;
  memset(&blob, 0, sizeof(blob));
  strncpy(blob.stop_id, board->stop_id, ID_LEN - 1);
  strncpy(blob.stop_name, board->stop_name, NAME_LEN - 1);
  strncpy(blob.fetched_at, board->fetched_at, TIME_LEN - 1);
  blob.count = board->count > PERSIST_BOARD_ROWS ? PERSIST_BOARD_ROWS
                                                 : board->count;
  for (uint8_t i = 0; i < blob.count; i++) {
    strncpy(blob.rows[i].line, board->items[i].line, LINE_LEN - 1);
    strncpy(blob.rows[i].dest, board->items[i].dest, PERSIST_DEST_LEN - 1);
    strncpy(blob.rows[i].time, board->items[i].time, TIME_LEN - 1);
  }
  persist_write_data(PERSIST_KEY_LAST_BOARD, &blob, sizeof(blob));
}

bool persist_load_board(const char *stop_id) {
  if (!persist_exists(PERSIST_KEY_LAST_BOARD)) {
    return false;
  }
  PersistBoard blob;
  int read = persist_read_data(PERSIST_KEY_LAST_BOARD, &blob, sizeof(blob));
  if (read < (int)sizeof(blob) || blob.count > PERSIST_BOARD_ROWS ||
      strncmp(blob.stop_id, stop_id, ID_LEN) != 0) {
    return false;
  }
  DepartureBoard *board = model_board();
  board->count = blob.count;
  board->expected = blob.count;
  board->flags = BOARD_FLAG_CACHED | BOARD_FLAG_STALE;
  board->error = ERR_NONE;
  board->loading = false;
  strncpy(board->stop_name, blob.stop_name, NAME_LEN - 1);
  board->stop_name[NAME_LEN - 1] = '\0';
  strncpy(board->fetched_at, blob.fetched_at, TIME_LEN - 1);
  board->fetched_at[TIME_LEN - 1] = '\0';
  for (uint8_t i = 0; i < blob.count; i++) {
    strncpy(board->items[i].line, blob.rows[i].line, LINE_LEN - 1);
    board->items[i].line[LINE_LEN - 1] = '\0';
    strncpy(board->items[i].dest, blob.rows[i].dest, DEST_LEN - 1);
    board->items[i].dest[DEST_LEN - 1] = '\0';
    strncpy(board->items[i].time, blob.rows[i].time, TIME_LEN - 1);
    board->items[i].time[TIME_LEN - 1] = '\0';
    board->items[i].delay = DELAY_UNKNOWN;
  }
  return true;
}

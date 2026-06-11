#include "comm.h"

#include "model.h"
#include "persist.h"

#if defined(PBL_PLATFORM_APLITE)
#define INBOX_SIZE 512
#else
#define INBOX_SIZE 1024
#endif
#define OUTBOX_SIZE 256

static CommUpdatedHandler s_board_handler;
static CommUpdatedHandler s_stops_handler;
static uint32_t s_request_counter;

static void prv_notify_board(void) {
  if (s_board_handler) {
    s_board_handler();
  }
}

static void prv_notify_stops(void) {
  if (s_stops_handler) {
    s_stops_handler();
  }
}

static void prv_copy_tuple_str(DictionaryIterator *iter, uint32_t key,
                               char *dest, size_t size) {
  Tuple *t = dict_find(iter, key);
  if (t && t->type == TUPLE_CSTRING) {
    strncpy(dest, t->value->cstring, size - 1);
    dest[size - 1] = '\0';
  }
}

static void prv_handle_departures_header(DictionaryIterator *iter,
                                         DepartureBoard *board) {
  Tuple *count = dict_find(iter, MESSAGE_KEY_META_COUNT);
  Tuple *error = dict_find(iter, MESSAGE_KEY_ERROR);
  Tuple *flags = dict_find(iter, MESSAGE_KEY_META_FLAGS);

  // Phone reached us but couldn't fetch: fall back to the persisted last board
  // (shown with the Offline badge), mirroring the Bluetooth-down path. Only
  // surface the bare error when no board is stored for this stop.
  uint8_t err = error ? error->value->uint8 : ERR_NONE;
  if (err != ERR_NONE && persist_load_board(board->stop_id)) {
    prv_notify_board();
    return;
  }

  board->count = 0;
  board->expected = count ? count->value->uint8 : 0;
  board->error = err;
  board->flags = flags ? flags->value->uint8 : 0;
  prv_copy_tuple_str(iter, MESSAGE_KEY_META_STOP_NAME, board->stop_name,
                     NAME_LEN);
  prv_copy_tuple_str(iter, MESSAGE_KEY_META_FETCHED_AT, board->fetched_at,
                     TIME_LEN);
  board->loading = (board->error == ERR_NONE && board->expected > 0);
  prv_notify_board();
}

static void prv_handle_departures_row(DictionaryIterator *iter,
                                      DepartureBoard *board, uint8_t index) {
  if (index >= MAX_DEPARTURES || index >= board->expected) {
    return;
  }
  Departure *dep = &board->items[index];
  prv_copy_tuple_str(iter, MESSAGE_KEY_ROW_LINE, dep->line, LINE_LEN);
  prv_copy_tuple_str(iter, MESSAGE_KEY_ROW_DEST, dep->dest, DEST_LEN);
  prv_copy_tuple_str(iter, MESSAGE_KEY_ROW_TIME, dep->time, TIME_LEN);
  Tuple *delay = dict_find(iter, MESSAGE_KEY_ROW_DELAY);
  dep->delay = delay ? delay->value->int32 : DELAY_UNKNOWN;

  if (index + 1 > board->count) {
    board->count = index + 1;
  }
  if (board->count >= board->expected) {
    board->loading = false;
    if (!(board->flags & BOARD_FLAG_CACHED)) {
      persist_store_board();  // fresh board — keep for offline fallback
    }
  }
  // Repaint on first row, completion, and every 4th row to limit flicker
  if (board->count == 1 || !board->loading || board->count % 4 == 0) {
    prv_notify_board();
  }
}

static void prv_handle_stops_header(DictionaryIterator *iter, uint8_t op) {
  StopsModel *stops = model_stops();
  Tuple *count = dict_find(iter, MESSAGE_KEY_META_COUNT);
  Tuple *error = dict_find(iter, MESSAGE_KEY_ERROR);
  uint8_t expected = count ? count->value->uint8 : 0;
  uint8_t err = error ? error->value->uint8 : ERR_NONE;

  if (op == OP_GET_NEAREST) {
    stops->nearest_count = 0;
    stops->nearest_expected =
        expected > MAX_NEAREST ? MAX_NEAREST : expected;
    stops->nearest_error = err;
    stops->nearest_loading = (err == ERR_NONE && expected > 0);
  } else {  // OP_FAVORITES
    stops->favorites_count = 0;
    if (expected > MAX_FAVORITES) {
      expected = MAX_FAVORITES;
    }
    if (expected == 0) {
      persist_store_favorites();
    }
  }
  prv_notify_stops();
}

static void prv_handle_stops_row(DictionaryIterator *iter, uint8_t op,
                                 uint8_t index) {
  StopsModel *stops = model_stops();
  StopRef *ref = NULL;

  if (op == OP_GET_NEAREST) {
    if (index >= MAX_NEAREST || index >= stops->nearest_expected) {
      return;
    }
    ref = &stops->nearest[index];
    if (index + 1 > stops->nearest_count) {
      stops->nearest_count = index + 1;
    }
    if (stops->nearest_count >= stops->nearest_expected) {
      stops->nearest_loading = false;
    }
  } else {  // OP_FAVORITES
    if (index >= MAX_FAVORITES) {
      return;
    }
    ref = &stops->favorites[index];
    if (index + 1 > stops->favorites_count) {
      stops->favorites_count = index + 1;
    }
  }

  // Stops rows reuse the departure row keys:
  // ROW_LINE = stop name, ROW_META = stop id, ROW_TIME = distance string
  prv_copy_tuple_str(iter, MESSAGE_KEY_ROW_LINE, ref->name, NAME_LEN);
  prv_copy_tuple_str(iter, MESSAGE_KEY_ROW_META, ref->id, ID_LEN);
  ref->dist[0] = '\0';
  prv_copy_tuple_str(iter, MESSAGE_KEY_ROW_TIME, ref->dist, DIST_LEN);

  if (op == OP_FAVORITES) {
    // Persist once the last pushed favorite arrived
    Tuple *count = dict_find(iter, MESSAGE_KEY_META_COUNT);
    if (count && stops->favorites_count >= count->value->uint8) {
      persist_store_favorites();
    }
  }
  prv_notify_stops();
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *op_tuple = dict_find(iter, MESSAGE_KEY_OP);
  Tuple *req_tuple = dict_find(iter, MESSAGE_KEY_REQUEST_ID);
  if (!op_tuple) {
    return;
  }
  uint8_t op = op_tuple->value->uint8;
  uint32_t request_id = req_tuple ? req_tuple->value->uint32 : 0;

  // Drop stale responses; unsolicited favorites pushes (request_id 0) pass
  if (op != OP_FAVORITES && request_id != s_request_counter) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "drop stale msg op=%d req=%lu cur=%lu",
            (int)op, (unsigned long)request_id,
            (unsigned long)s_request_counter);
    return;
  }

  Tuple *row_index = dict_find(iter, MESSAGE_KEY_ROW_INDEX);
  bool is_row = (row_index != NULL);

  if (op == OP_GET_DEPARTURES) {
    DepartureBoard *board = model_board();
    if (is_row) {
      prv_handle_departures_row(iter, board, row_index->value->uint8);
    } else {
      prv_handle_departures_header(iter, board);
    }
  } else if (op == OP_GET_NEAREST || op == OP_FAVORITES) {
    if (is_row) {
      prv_handle_stops_row(iter, op, row_index->value->uint8);
    } else {
      prv_handle_stops_header(iter, op);
    }
  }
}

static void prv_inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "inbox dropped: %d", (int)reason);
}

// Phone unreachable — fall back to the persisted last board if it matches
static void prv_departures_send_failed(void) {
  DepartureBoard *board = model_board();
  board->loading = false;
  if (!persist_load_board(board->stop_id)) {
    board->error = ERR_NETWORK;
  }
  prv_notify_board();
}

static void prv_outbox_failed(DictionaryIterator *iter, AppMessageResult reason,
                              void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "outbox failed: %d", (int)reason);
  Tuple *op_tuple = dict_find(iter, MESSAGE_KEY_OP);
  uint8_t op = op_tuple ? op_tuple->value->uint8 : 0;
  if (op == OP_GET_DEPARTURES) {
    prv_departures_send_failed();
  } else if (op == OP_GET_NEAREST) {
    StopsModel *stops = model_stops();
    stops->nearest_loading = false;
    stops->nearest_error = ERR_NETWORK;
    prv_notify_stops();
  }
}

// stop_name is sent only for departures: ids on api.dpmhk.cz are packet-scoped
// (the weekly timetable reassigns them), so a favorite saved under a previous
// packet holds a now-stale id. The phone re-resolves the name to the current
// packet's id, making taps correct even before the favorites mirror refreshes.
static bool prv_send_request_with_id(uint8_t op, const char *stop_id,
                                     const char *stop_name,
                                     uint32_t request_id) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return false;
  }
  dict_write_uint8(iter, MESSAGE_KEY_OP, op);
  dict_write_uint32(iter, MESSAGE_KEY_REQUEST_ID, request_id);
  if (stop_id) {
    dict_write_cstring(iter, MESSAGE_KEY_STOP_ID, stop_id);
  }
  if (stop_name && stop_name[0]) {
    dict_write_cstring(iter, MESSAGE_KEY_META_STOP_NAME, stop_name);
  }
  return app_message_outbox_send() == APP_MSG_OK;
}

static bool prv_send_request(uint8_t op, const char *stop_id) {
  return prv_send_request_with_id(op, stop_id, NULL, s_request_counter);
}

void comm_request_departures(const char *stop_id, const char *stop_name) {
  s_request_counter++;
  model_board_begin_request(s_request_counter, stop_id, stop_name);
  prv_notify_board();
  if (!prv_send_request_with_id(OP_GET_DEPARTURES, stop_id, stop_name,
                                s_request_counter)) {
    prv_departures_send_failed();
  }
}

void comm_request_nearest(void) {
  s_request_counter++;
  StopsModel *stops = model_stops();
  stops->nearest_count = 0;
  stops->nearest_expected = 0;
  stops->nearest_error = ERR_NONE;
  stops->nearest_loading = true;
  prv_notify_stops();
  if (!prv_send_request(OP_GET_NEAREST, NULL)) {
    stops->nearest_loading = false;
    stops->nearest_error = ERR_NETWORK;
    prv_notify_stops();
  }
}

// Fire-and-forget: sent with REQUEST_ID 0 and WITHOUT bumping the request
// counter — bumping it would strand in-flight tracked replies (their rows
// would fail the stale-id check and e.g. nearest_loading would never clear)
void comm_add_favorite(const char *stop_id) {
  prv_send_request_with_id(OP_ADD_FAVORITE, stop_id, NULL, 0);
}

void comm_remove_favorite(const char *stop_id) {
  prv_send_request_with_id(OP_REMOVE_FAVORITE, stop_id, NULL, 0);
}

void comm_set_board_handler(CommUpdatedHandler handler) {
  s_board_handler = handler;
}

void comm_set_stops_handler(CommUpdatedHandler handler) {
  s_stops_handler = handler;
}

void comm_init(void) {
  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_register_outbox_failed(prv_outbox_failed);
  app_message_open(INBOX_SIZE, OUTBOX_SIZE);
}

void comm_deinit(void) {
  app_message_deregister_callbacks();
}

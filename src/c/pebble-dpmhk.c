#include <pebble.h>

#include "comm.h"
#include "persist.h"
#include "windows/stops_window.h"

static void prv_init(void) {
  persist_load_favorites();
  comm_init();
  stops_window_push();
}

static void prv_deinit(void) {
  comm_deinit();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

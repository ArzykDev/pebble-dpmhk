#include <pebble.h>

#include "comm.h"
#include "persist.h"
#include "reminder.h"
#include "windows/departures_window.h"
#include "windows/stops_window.h"

static void prv_init(void) {
#if defined(PBL_TOUCH)
  // Apps are opted out by default; this lets the MenuLayers scroll and
  // activate rows by touch with no per-window code.
  app_touch_navigation_enable(true);
#endif
  persist_load_favorites();
  comm_init();
  reminder_init();
  // The stop list stays underneath: Back from the launch board reaches it
  stops_window_push();
  StopRef reminded;
  if (reminder_take_launch(&reminded)) {
    departures_window_push(&reminded);
  } else {
    departures_window_push_auto();
  }
}

static void prv_deinit(void) {
  comm_deinit();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

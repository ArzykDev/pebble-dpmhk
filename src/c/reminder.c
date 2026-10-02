#include "reminder.h"

#include "ui_theme.h"
#include "windows/departures_window.h"

#define PERSIST_KEY_REMINDER 3
#define PERSIST_KEY_REMIND_LEAD 4
#define DEFAULT_LEAD_MIN 5

typedef struct __attribute__((__packed__)) {
  int32_t wakeup_id;
  char stop_id[ID_LEN];
  char stop_name[NAME_LEN];
  char line[LINE_LEN];
  char dest[DEST_LEN];
} Reminder;  // 92 bytes

static bool prv_load(Reminder *r) {
  return persist_read_data(PERSIST_KEY_REMINDER, r, sizeof(*r)) ==
         (int)sizeof(*r);
}

static void prv_open_stop(const Reminder *r, StopRef *stop) {
  memset(stop, 0, sizeof(*stop));
  strncpy(stop->id, r->stop_id, ID_LEN - 1);
  strncpy(stop->name, r->stop_name, NAME_LEN - 1);
}

static void prv_wakeup(WakeupId id, int32_t cookie) {
  Reminder r;
  if (!prv_load(&r)) {
    return;
  }
  persist_delete(PERSIST_KEY_REMINDER);
  vibes_double_pulse();
  StopRef stop;
  prv_open_stop(&r, &stop);
  departures_window_push(&stop);
}

void reminder_init(void) {
  wakeup_service_subscribe(prv_wakeup);
}

bool reminder_take_launch(StopRef *stop) {
  WakeupId id;
  int32_t cookie;
  Reminder r;
  if (launch_reason() != APP_LAUNCH_WAKEUP ||
      !wakeup_get_launch_event(&id, &cookie) || !prv_load(&r)) {
    return false;
  }
  persist_delete(PERSIST_KEY_REMINDER);
  vibes_double_pulse();
  prv_open_stop(&r, stop);
  return true;
}

bool reminder_is_set(const char *line, const char *dest) {
  Reminder r;
  return prv_load(&r) && wakeup_query(r.wakeup_id, NULL) &&
         strcmp(r.line, line) == 0 && strcmp(r.dest, dest) == 0;
}

uint8_t reminder_lead(void) {
  return persist_exists(PERSIST_KEY_REMIND_LEAD)
             ? persist_read_int(PERSIST_KEY_REMIND_LEAD)
             : DEFAULT_LEAD_MIN;
}

bool reminder_set(const char *line, const char *dest, const char *hhmm) {
  int lead = reminder_lead();
  int mins = theme_minutes_until(hhmm);
  if (mins == THEME_MIN_INVALID || mins - lead < 1) {
    return false;  // already inside the lead time
  }
  time_t now = time(NULL);
  time_t when = now - now % 60 + (mins - lead) * 60;
  reminder_cancel();
  WakeupId id = wakeup_schedule(when, 0, true);
  if (id < 0) {
    return false;
  }
  const DepartureBoard *board = model_board();
  Reminder r;
  memset(&r, 0, sizeof(r));
  r.wakeup_id = id;
  strncpy(r.stop_id, board->stop_id, ID_LEN - 1);
  strncpy(r.stop_name, board->stop_name, NAME_LEN - 1);
  strncpy(r.line, line, LINE_LEN - 1);
  strncpy(r.dest, dest, DEST_LEN - 1);
  persist_write_data(PERSIST_KEY_REMINDER, &r, sizeof(r));
  return true;
}

void reminder_cancel(void) {
  wakeup_cancel_all();
  persist_delete(PERSIST_KEY_REMINDER);
}

void reminder_set_lead(uint8_t minutes) {
  persist_write_int(PERSIST_KEY_REMIND_LEAD, minutes);
}

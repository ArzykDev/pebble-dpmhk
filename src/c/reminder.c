#include "reminder.h"

#include "comm.h"
#include "ui_theme.h"

#define PERSIST_KEY_REMINDER 3
#define PERSIST_KEY_REMIND_LEAD 4
#define DEFAULT_LEAD_MIN 5

// What the watch remembers about the pin, only to label the route screen's
// toggle; the pin itself lives on the phone.
typedef struct __attribute__((__packed__)) {
  char line[LINE_LEN];
  char dest[DEST_LEN];
  char time[TIME_LEN];
} Reminder;

static bool prv_load(Reminder *r) {
  return persist_read_data(PERSIST_KEY_REMINDER, r, sizeof(*r)) ==
         (int)sizeof(*r);
}

bool reminder_is_set(const char *line, const char *dest) {
  Reminder r;
  if (!prv_load(&r) || strcmp(r.line, line) != 0 ||
      strcmp(r.dest, dest) != 0) {
    return false;
  }
  int mins = theme_minutes_until(r.time);
  return mins != THEME_MIN_INVALID && mins >= 0;  // a departed one is spent
}

uint8_t reminder_lead(void) {
  return persist_exists(PERSIST_KEY_REMIND_LEAD)
             ? persist_read_int(PERSIST_KEY_REMIND_LEAD)
             : DEFAULT_LEAD_MIN;
}

bool reminder_too_soon(const char *hhmm) {
  int mins = theme_minutes_until(hhmm);
  return mins == THEME_MIN_INVALID || mins - reminder_lead() < 1;
}

bool reminder_set(const char *line, const char *dest, const char *hhmm) {
  if (reminder_too_soon(hhmm)) {
    return false;
  }
  if (!comm_send_reminder(line, dest, hhmm, model_board()->stop_name)) {
    return false;
  }
  Reminder r;
  memset(&r, 0, sizeof(r));
  strncpy(r.line, line, LINE_LEN - 1);
  strncpy(r.dest, dest, DEST_LEN - 1);
  strncpy(r.time, hhmm, TIME_LEN - 1);
  persist_write_data(PERSIST_KEY_REMINDER, &r, sizeof(r));
  return true;
}

void reminder_cancel(void) {
  comm_send_reminder("", "", "", "");
  persist_delete(PERSIST_KEY_REMINDER);
}

void reminder_set_lead(uint8_t minutes) {
  persist_write_int(PERSIST_KEY_REMIND_LEAD, minutes);
}

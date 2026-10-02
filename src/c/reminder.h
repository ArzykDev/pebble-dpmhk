#pragma once
#include <pebble.h>

#include "model.h"

// One departure reminder at a time: a wakeup fires the lead time before the
// departure, vibrates, and opens that stop's board.

// Subscribe to wakeups that fire while the app is open
void reminder_init(void);

// True when the app was launched by our wakeup; fills the stop to open and
// clears the reminder
bool reminder_take_launch(StopRef *stop);

// Whether the pending reminder is for this line+destination
bool reminder_is_set(const char *line, const char *dest);

// Remind about the "HH:MM" departure of line+dest from the current board's
// stop. False when it is too soon for the lead time or scheduling failed.
bool reminder_set(const char *line, const char *dest, const char *hhmm);

void reminder_cancel(void);

// Lead time in minutes (phone setting)
uint8_t reminder_lead(void);
void reminder_set_lead(uint8_t minutes);

#pragma once
#include <pebble.h>

#include "model.h"

// One departure reminder at a time. The phone turns it into a timeline pin
// whose reminder fires the lead time before the departure, so the watch shows
// the system reminder card (and the pin sits in the timeline).

// Whether the pending reminder is for this line+destination
bool reminder_is_set(const char *line, const char *dest);

// Remind about the "HH:MM" departure of line+dest from the current board's
// stop. False when it is too soon for the lead time or the phone is
// unreachable.
bool reminder_set(const char *line, const char *dest, const char *hhmm);

void reminder_cancel(void);

// Lead time in minutes (phone setting)
uint8_t reminder_lead(void);
void reminder_set_lead(uint8_t minutes);

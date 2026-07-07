#pragma once
#include <limits.h>

#include <pebble.h>

// Returned by theme_minutes_until when the time string can't be parsed.
#define THEME_MIN_INVALID INT_MIN

// Deterministic per-line brand color (hash into a curated palette). DPMHK has
// no official per-line palette, so the same line always maps to the same color.
// Color platforms only — callers guard with PBL_IF_COLOR_ELSE.
GColor theme_line_color(const char *line);

// Draw the line number as a filled rounded-rect chip (white text over the line
// color) centered in rect. On mono platforms falls back to plain bold text.
void theme_draw_line_badge(GContext *ctx, GRect rect, const char *line,
                           bool highlighted);

// Apply the DPMHK-branded selection highlight to a menu (works on every
// platform; degrades to black/white on mono).
void theme_apply_menu(MenuLayer *menu);

// Minutes from now until the "HH:MM" departure, handling the 23:00 next-day
// merge. THEME_MIN_INVALID on a malformed string.
int theme_minutes_until(const char *hhmm);

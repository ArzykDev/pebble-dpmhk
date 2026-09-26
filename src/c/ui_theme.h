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

// Filled five-point star (favorite marker) of outer radius r centered at c.
void theme_draw_star(GContext *ctx, GPoint c, int r, GColor color);

// Draw a space-separated line list as small chips left to right, ending in an
// ellipsis when they don't all fit. Mono platforms draw the plain list.
void theme_draw_line_chips(GContext *ctx, GRect rect, const char *lines,
                           bool highlighted);

// Draw a PebbleOS system icon resource (PDC; a 1-bit PNG on aplite) with its
// top-left at origin, recolored for a highlighted row. Returns its size.
GSize theme_draw_icon(GContext *ctx, uint32_t resource_id, GPoint origin,
                      bool highlighted);

// Full-row empty/error state: a plain card with the icon centered above the
// text (resource_id 0 = text only, e.g. the animated loading message).
void theme_draw_status(GContext *ctx, GRect rect, uint32_t resource_id,
                       const char *text);

// Error text for a failed fetch: a dead phone link reads differently from a
// phone that is up but can't reach the API. Sets *icon to the matching
// status (large) or small icon.
const char *theme_error_status(bool small, uint32_t *icon);

// Apply the DPMHK-branded selection highlight to a menu (works on every
// platform; degrades to black/white on mono).
void theme_apply_menu(MenuLayer *menu);

// Minutes from now until the "HH:MM" departure, handling the 23:00 next-day
// merge. THEME_MIN_INVALID on a malformed string.
int theme_minutes_until(const char *hhmm);

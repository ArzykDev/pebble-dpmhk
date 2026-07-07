#include "ui_theme.h"

// Branded selection color. DPMHK's identity is orange; degrades to the default
// black highlight on mono.
#define THEME_ACCENT PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack)

GColor theme_line_color(const char *line) {
#if defined(PBL_COLOR)
  // Curated dark palette — white text sits legibly on each.
  static const uint8_t palette[] = {
      GColorDukeBlueARGB8,       GColorCobaltBlueARGB8,
      GColorIslamicGreenARGB8,   GColorKellyGreenARGB8,
      GColorImperialPurpleARGB8, GColorPurpleARGB8,
      GColorBulgarianRoseARGB8,  GColorWindsorTanARGB8,
  };
  uint32_t h = 0;
  for (const char *p = line; *p; p++) {
    if (*p != ' ') {
      h = h * 31u + (uint8_t)*p;
    }
  }
  return (GColor){.argb = palette[h % (sizeof(palette))]};
#else
  (void)line;
  return GColorBlack;
#endif
}

void theme_draw_line_badge(GContext *ctx, GRect rect, const char *line,
                           bool highlighted) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#if defined(PBL_COLOR)
  (void)highlighted;
  GSize ts = graphics_text_layout_get_content_size(
      line, font, rect, GTextOverflowModeFill, GTextAlignmentLeft);
  int chip_h = 26;
  int chip_w = ts.w + 12;
  if (chip_w > rect.size.w) {
    chip_w = rect.size.w;
  }
  GRect chip = GRect(rect.origin.x, rect.origin.y + (rect.size.h - chip_h) / 2,
                     chip_w, chip_h);
  graphics_context_set_fill_color(ctx, theme_line_color(line));
  graphics_fill_rect(ctx, chip, 4, GCornersAll);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, line, font,
                     GRect(chip.origin.x, chip.origin.y - 4, chip.size.w,
                           chip.size.h),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
#else
  graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, line, font, rect, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
#endif
}

void theme_apply_menu(MenuLayer *menu) {
  menu_layer_set_highlight_colors(menu, THEME_ACCENT, GColorWhite);
}

int theme_minutes_until(const char *hhmm) {
  if (!hhmm) {
    return THEME_MIN_INVALID;
  }
  // Parse "H:MM"/"HH:MM" by hand — sscanf drags in newlib's setlocale, which
  // collides with libpebble's own definition at link time.
  const char *p = hhmm;
  int h = 0, m = 0, hd = 0, md = 0;
  while (*p >= '0' && *p <= '9') {
    h = h * 10 + (*p++ - '0');
    hd++;
  }
  if (hd == 0 || hd > 2 || *p != ':') {
    return THEME_MIN_INVALID;
  }
  p++;
  while (*p >= '0' && *p <= '9') {
    m = m * 10 + (*p++ - '0');
    md++;
  }
  if (md == 0 || h > 23 || m > 59) {
    return THEME_MIN_INVALID;
  }
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  int diff = (h * 60 + m) - (lt->tm_hour * 60 + lt->tm_min);
  // The board merges the next day's early departures (23:00 rollover); a large
  // negative gap means "tomorrow", not "long departed".
  if (diff <= -120) {
    diff += 1440;
  }
  return diff;
}

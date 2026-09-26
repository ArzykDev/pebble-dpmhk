#include "ui_theme.h"

#include "model.h"
#include "strings.h"

// Selection highlight: a dark near-neutral so the colored line badges pop
// against it rather than clashing (a saturated bar would fight the badges).
#define THEME_ACCENT PBL_IF_COLOR_ELSE(GColorOxfordBlue, GColorBlack)

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

#if defined(PBL_COLOR)
// Filled chip vertically centered in rect; returns its width. text_dy lifts
// the glyphs to sit visually centered (system fonts carry top padding).
static int prv_draw_chip(GContext *ctx, GRect rect, const char *line,
                         GFont font, int chip_h, int pad, int text_dy,
                         bool highlighted) {
  // On the (dark) highlighted row the dark badge would vanish, so invert it:
  // a white chip carrying the line color as the number.
  GColor chip_color = highlighted ? GColorWhite : theme_line_color(line);
  GColor text_color = highlighted ? theme_line_color(line) : GColorWhite;
  GSize ts = graphics_text_layout_get_content_size(
      line, font, rect, GTextOverflowModeFill, GTextAlignmentLeft);
  int chip_w = ts.w + 2 * pad;
  if (chip_w > rect.size.w) {
    chip_w = rect.size.w;
  }
  GRect chip = GRect(rect.origin.x, rect.origin.y + (rect.size.h - chip_h) / 2,
                     chip_w, chip_h);
  graphics_context_set_fill_color(ctx, chip_color);
  graphics_fill_rect(ctx, chip, chip_h > 20 ? 4 : 3, GCornersAll);
  graphics_context_set_text_color(ctx, text_color);
  graphics_draw_text(ctx, line, font,
                     GRect(chip.origin.x, chip.origin.y + text_dy, chip.size.w,
                           chip.size.h),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  return chip_w;
}
#endif

void theme_draw_line_badge(GContext *ctx, GRect rect, const char *line,
                           bool highlighted) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#if defined(PBL_COLOR)
  prv_draw_chip(ctx, rect, line, font, 26, 6, -4, highlighted);
#else
  graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, line, font, rect, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
#endif
}

void theme_draw_line_chips(GContext *ctx, GRect rect, const char *lines,
                           bool highlighted) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
#if defined(PBL_COLOR)
  const int gap = 3;
  const int ellipsis_w = 12;
  int x = rect.origin.x;
  int right = rect.origin.x + rect.size.w;
  const char *p = lines;
  while (*p) {
    while (*p == ' ') {
      p++;
    }
    const char *end = p;
    while (*end && *end != ' ') {
      end++;
    }
    if (end == p) {
      break;
    }
    char line[LINE_LEN];
    int n = end - p < LINE_LEN - 1 ? end - p : LINE_LEN - 1;
    memcpy(line, p, n);
    line[n] = '\0';
    GSize ts = graphics_text_layout_get_content_size(
        line, font, rect, GTextOverflowModeFill, GTextAlignmentLeft);
    bool last = *end == '\0';
    // Keep room for the ellipsis unless this chip is the final one
    int limit = last ? right : right - ellipsis_w;
    if (x + ts.w + 8 > limit) {
      graphics_context_set_text_color(ctx,
                                      highlighted ? GColorWhite : GColorBlack);
      graphics_draw_text(ctx, "…", font,
                         GRect(x, rect.origin.y - 3, ellipsis_w, rect.size.h),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
      return;
    }
    x += prv_draw_chip(ctx, GRect(x, rect.origin.y, right - x, rect.size.h),
                       line, font, rect.size.h, 4, -3, highlighted) +
         gap;
    p = end;
  }
#else
  graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, lines, font, GRect(rect.origin.x, rect.origin.y - 3,
                                             rect.size.w, rect.size.h + 3),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
#endif
}

void theme_draw_star(GContext *ctx, GPoint c, int r, GColor color) {
  GPoint pts[10];
  for (int i = 0; i < 10; i++) {
    // Fatter than a geometric star: thin arms drop out when filled this small
    int rad = (i % 2) ? r * 11 / 20 : r;
    int32_t a = TRIG_MAX_ANGLE * i / 10;
    pts[i] = GPoint(c.x + rad * sin_lookup(a) / TRIG_MAX_RATIO,
                    c.y - rad * cos_lookup(a) / TRIG_MAX_RATIO);
  }
  GPathInfo info = {.num_points = 10, .points = pts};
  GPath *path = gpath_create(&info);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

#if !defined(PBL_PLATFORM_APLITE)
// The system icons are drawn black on white; swap to the row's highlight
// colors so they stay visible on the selection bar.
static bool prv_invert_command(GDrawCommand *command, uint32_t index,
                               void *context) {
  GColor stroke = gdraw_command_get_stroke_color(command);
  GColor fill = gdraw_command_get_fill_color(command);
  if (gcolor_equal(stroke, GColorBlack)) {
    gdraw_command_set_stroke_color(command, GColorWhite);
  }
  if (gcolor_equal(fill, GColorWhite)) {
    gdraw_command_set_fill_color(command, THEME_ACCENT);
  }
  return true;
}
#endif

// ponytail: loads and frees the resource on every draw (a few hundred bytes);
// cache per window if redraw cost ever shows up.
GSize theme_draw_icon(GContext *ctx, uint32_t resource_id, GPoint origin,
                      bool highlighted) {
#if defined(PBL_PLATFORM_APLITE)
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) {
    return GSizeZero;
  }
  GSize size = gbitmap_get_bounds(bmp).size;
  // PNG resources load as 1BitPalette, which ignores inverted compositing, so
  // invert through the (per-draw, owned) palette instead.
  GColor *palette = gbitmap_get_palette(bmp);
  if (highlighted && palette) {
    for (int i = 0; i < 2; i++) {
      palette[i] = gcolor_equal(palette[i], GColorBlack) ? GColorWhite
                                                         : GColorBlack;
    }
  }
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  graphics_draw_bitmap_in_rect(ctx, bmp, (GRect){.origin = origin, .size = size});
  gbitmap_destroy(bmp);
#else
  GDrawCommandImage *img = gdraw_command_image_create_with_resource(resource_id);
  if (!img) {
    return GSizeZero;
  }
  GSize size = gdraw_command_image_get_bounds_size(img);
  if (highlighted) {
    gdraw_command_list_iterate(gdraw_command_image_get_command_list(img),
                               prv_invert_command, NULL);
  }
  gdraw_command_image_draw(ctx, img, origin);
  gdraw_command_image_destroy(img);
#endif
  return size;
}

void theme_draw_status(GContext *ctx, GRect rect, uint32_t resource_id,
                       const char *text) {
  // A plain card over the selection bar, like the system's empty states
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, rect, 0, GCornerNone);

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  const int icon_size = 50;  // every status icon is a 50x50 system icon
  const int gap = 6;
  int text_w = rect.size.w - 2 * PBL_IF_ROUND_ELSE(18, 8);
  GSize ts = graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, text_w, 48), GTextOverflowModeWordWrap,
      GTextAlignmentCenter);
  int h = ts.h + (resource_id ? icon_size + gap : 0);
  int y = rect.origin.y + (rect.size.h - h) / 2;
  if (resource_id) {
    theme_draw_icon(ctx, resource_id,
                    GPoint(rect.origin.x + (rect.size.w - icon_size) / 2, y),
                    false);
    y += icon_size + gap;
  }
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, text, font,
                     GRect(rect.origin.x + (rect.size.w - text_w) / 2, y - 4,
                           text_w, ts.h + 4),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

const char *theme_error_status(bool small, uint32_t *icon) {
  if (!connection_service_peek_pebble_app_connection()) {
    *icon = small ? RESOURCE_ID_ICON_SMALL_WARNING
                  : RESOURCE_ID_ICON_STATUS_DISCONNECTED;
    return STR_NO_PHONE;
  }
  *icon = small ? RESOURCE_ID_ICON_SMALL_WARNING
                : RESOURCE_ID_ICON_STATUS_NO_CONNECTION;
  return STR_CONN_ERROR;
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

#include "trip_window.h"

#include "../comm.h"
#include "../model.h"
#include "../reminder.h"
#include "../strings.h"
#include "../ui_theme.h"

#define ROW_HEIGHT 32
#define REMIND_ROW_HEIGHT 36
#define SPINE_X PBL_IF_ROUND_ELSE(23, 9)
// The reminder row heads the screen, then the route
#define SECTION_REMIND 0
#define SECTION_ROUTE 1

static Window *s_window;
static MenuLayer *s_menu_layer;
static StatusBarLayer *s_status_bar;
static char s_header_text[LINE_LEN + DEST_LEN + 8];
static char s_time[TIME_LEN];  // next departure of this line, for reminders
// Reminder row state, refreshed on push/select/update rather than per draw
// (it reads watch storage)
typedef enum { REMIND_HIDDEN, REMIND_OFFER, REMIND_SOON, REMIND_SET } RemindRow;
static RemindRow s_remind;

static void prv_update_remind(const char *line, const char *dest) {
  int mins = theme_minutes_until(s_time);
  s_remind = mins == THEME_MIN_INVALID || mins < 0 ? REMIND_HIDDEN
             : reminder_is_set(line, dest)          ? REMIND_SET
             : reminder_too_soon(s_time)            ? REMIND_SOON
                                                    : REMIND_OFFER;
}

static const char *prv_status_message(const TripModel *trip, uint32_t *icon) {
  if (trip->error != ERR_NONE) {
    return theme_error_status(trip->error, false, icon);
  }
  if (trip->loading) {
    *icon = 0;
    return STR_LOADING;
  }
  *icon = RESOURCE_ID_ICON_STATUS_WARNING;
  return STR_TRIP_EMPTY;
}

static uint16_t prv_get_num_sections(MenuLayer *menu_layer, void *context) {
  return 2;
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index,
                                 void *context) {
  if (section_index == SECTION_REMIND) {
    return s_remind == REMIND_HIDDEN ? 0 : 1;
  }
  const TripModel *trip = model_trip();
  return trip->count > 0 ? trip->count : 1;
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index,
                                   void *context) {
  if (cell_index->section == SECTION_REMIND) {
    return REMIND_ROW_HEIGHT;
  }
  if (model_trip()->count > 0) {
    return ROW_HEIGHT;
  }
  // The empty state fills the screen below the header and reminder row
  GRect frame = layer_get_bounds(menu_layer_get_layer(menu_layer));
  return frame.size.h - MENU_CELL_BASIC_HEADER_HEIGHT -
         prv_get_num_rows(menu_layer, SECTION_REMIND, NULL) * REMIND_ROW_HEIGHT;
}

static int16_t prv_get_header_height(MenuLayer *menu_layer,
                                     uint16_t section_index, void *context) {
  return section_index == SECTION_REMIND ? MENU_CELL_BASIC_HEADER_HEIGHT : 0;
}

static void prv_draw_header(GContext *ctx, const Layer *cell_layer,
                            uint16_t section_index, void *context) {
  const TripModel *trip = model_trip();
  snprintf(s_header_text, sizeof(s_header_text), "%s " STR_TRIP_ARROW " %s",
           trip->line, trip->dest);
  theme_draw_header(ctx, cell_layer, s_header_text);
}

// "⏰ Připomenout 18:42", or its cancel when one is set for this line
static void prv_draw_remind(GContext *ctx, const Layer *cell_layer) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  char text[32];
  if (s_remind == REMIND_SET) {
    snprintf(text, sizeof(text), "%s", STR_REMIND_CANCEL);
  } else if (s_remind == REMIND_SOON) {
    snprintf(text, sizeof(text), STR_REMIND_SOON_FMT, s_time);
  } else {
    snprintf(text, sizeof(text), STR_REMIND_FMT, s_time);
  }
  const int icon_size = 25;
  int margin = theme_row_inset(cell_layer, (bounds.size.h - 24) / 2, 24);
  int x = margin + 2;
  theme_draw_icon(ctx, RESOURCE_ID_ICON_SMALL_ALARM,
                  GPoint(x, (bounds.size.h - icon_size) / 2), highlighted);
  x += icon_size + 6;
  graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(x, (bounds.size.h - 24) / 2,
                           bounds.size.w - x - margin, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer,
                         MenuIndex *cell_index, void *context) {
  if (cell_index->section == SECTION_REMIND) {
    prv_draw_remind(ctx, cell_layer);
    return;
  }
  const TripModel *trip = model_trip();
  GRect bounds = layer_get_bounds(cell_layer);

  if (trip->count == 0) {
    uint32_t icon;
    const char *text = prv_status_message(trip, &icon);
    theme_draw_status(ctx, bounds, icon, text);
    return;
  }

  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  int mid_y = bounds.size.h / 2;
  // The spine stays straight (a fixed x), only the name's right edge follows
  // the round display
  int margin = theme_row_inset(cell_layer, 2, 26);
  int spine_x = SPINE_X;
  GColor accent =
      highlighted ? GColorWhite
                  : PBL_IF_COLOR_ELSE(theme_line_color(trip->line), GColorBlack);

  // Route spine: a continuous line down the column, capped at the first/last
  // stop so the diagram reads as an ordered path.
  graphics_context_set_stroke_color(ctx, accent);
  graphics_context_set_stroke_width(ctx, 3);
  int top = (cell_index->row == 0) ? mid_y : 0;
  int bot = (cell_index->row == trip->count - 1) ? mid_y : bounds.size.h;
  graphics_draw_line(ctx, GPoint(spine_x, top), GPoint(spine_x, bot));

  // Station marker: a filled node, hollowed to a ring on normal rows
  graphics_context_set_fill_color(ctx, accent);
  graphics_fill_circle(ctx, GPoint(spine_x, mid_y), 5);
  if (!highlighted) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(spine_x, mid_y), 2);
  }

  int name_x = spine_x + 12;
  graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, trip->stops[cell_index->row],
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(name_x, 2, bounds.size.w - name_x - margin, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index,
                             void *context) {
  if (cell_index->section != SECTION_REMIND) {
    return;
  }
  const TripModel *trip = model_trip();
  if (s_remind == REMIND_SET) {
    reminder_cancel();
  } else if (reminder_set(trip->line, trip->dest, s_time)) {
    vibes_short_pulse();  // confirm: it's set
  }
  prv_update_remind(trip->line, trip->dest);
  menu_layer_reload_data(menu_layer);
}

static void prv_trip_updated(void) {
  if (s_menu_layer) {
    menu_layer_reload_data(s_menu_layer);
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_menu_layer = menu_layer_create(
      GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w,
            bounds.size.h - STATUS_BAR_LAYER_HEIGHT));
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
      .get_num_sections = prv_get_num_sections,
      .get_num_rows = prv_get_num_rows,
      .get_cell_height = prv_get_cell_height,
      .get_header_height = prv_get_header_height,
      .draw_header = prv_draw_header,
      .draw_row = prv_draw_row,
      .select_click = prv_select_click,
  });
#if defined(PBL_ROUND)
  menu_layer_set_center_focused(s_menu_layer, true);
#endif
  theme_apply_menu(s_menu_layer);
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_separator_mode(s_status_bar,
                                      StatusBarLayerSeparatorModeDotted);
  layer_add_child(window_layer, status_bar_layer_get_layer(s_status_bar));

  comm_set_trip_handler(prv_trip_updated);
}

static void prv_window_unload(Window *window) {
  comm_set_trip_handler(NULL);
  status_bar_layer_destroy(s_status_bar);
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void trip_window_push(const char *line, const char *dest, const char *time) {
  strncpy(s_time, time, TIME_LEN - 1);
  s_time[TIME_LEN - 1] = '\0';
  // Before the push, so the menu is built with the row (and selects it)
  prv_update_remind(line, dest);
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = prv_window_load,
        .unload = prv_window_unload,
    });
  }
  window_stack_push(s_window, true);
  comm_request_trip(line, dest);
}

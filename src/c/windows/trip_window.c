#include "trip_window.h"

#include "../comm.h"
#include "../model.h"
#include "../strings.h"

#define ROW_HEIGHT 32
#define STATUS_ROW_HEIGHT 32
#define MARGIN PBL_IF_ROUND_ELSE(18, 4)

static Window *s_window;
static MenuLayer *s_menu_layer;
static StatusBarLayer *s_status_bar;
static char s_header_text[LINE_LEN + DEST_LEN + 8];

static const char *prv_status_message(const TripModel *trip) {
  if (trip->error != ERR_NONE) {
    return STR_CONN_ERROR;
  }
  return trip->loading ? STR_LOADING : STR_TRIP_EMPTY;
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index,
                                 void *context) {
  const TripModel *trip = model_trip();
  return trip->count > 0 ? trip->count : 1;
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index,
                                   void *context) {
  return model_trip()->count > 0 ? ROW_HEIGHT : STATUS_ROW_HEIGHT;
}

static int16_t prv_get_header_height(MenuLayer *menu_layer,
                                     uint16_t section_index, void *context) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void prv_draw_header(GContext *ctx, const Layer *cell_layer,
                            uint16_t section_index, void *context) {
  const TripModel *trip = model_trip();
  snprintf(s_header_text, sizeof(s_header_text), "%s → %s", trip->line,
           trip->dest);
  menu_cell_basic_header_draw(ctx, cell_layer, s_header_text);
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer,
                         MenuIndex *cell_index, void *context) {
  const TripModel *trip = model_trip();
  GRect bounds = layer_get_bounds(cell_layer);

  if (trip->count == 0) {
    graphics_draw_text(ctx, prv_status_message(trip),
                       fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(MARGIN, 2, bounds.size.w - 2 * MARGIN, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                       NULL);
    return;
  }

  menu_cell_basic_draw(ctx, cell_layer, trip->stops[cell_index->row], NULL,
                       NULL);
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
      .get_num_rows = prv_get_num_rows,
      .get_cell_height = prv_get_cell_height,
      .get_header_height = prv_get_header_height,
      .draw_header = prv_draw_header,
      .draw_row = prv_draw_row,
  });
#if defined(PBL_ROUND)
  menu_layer_set_center_focused(s_menu_layer, true);
#endif
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

void trip_window_push(const char *line, const char *dest) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = prv_window_load,
        .unload = prv_window_unload,
    });
  }
  window_stack_push(s_window, true);
  comm_request_trip(line, dest, model_board()->stop_name);
}

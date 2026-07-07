#include "departures_window.h"

#include "../comm.h"
#include "../strings.h"
#include "../ui_theme.h"
#include "trip_window.h"

#define ROW_HEIGHT 44
#define STATUS_ROW_HEIGHT 32
#define MARGIN PBL_IF_ROUND_ELSE(18, 4)
#define LINE_BOX_W 40
#define TIME_BOX_W 56

static Window *s_window;
static MenuLayer *s_menu_layer;
static StatusBarLayer *s_status_bar;
static char s_header_text[NAME_LEN + TIME_LEN + 12];

static const char *prv_status_message(const DepartureBoard *board) {
  if (board->error == ERR_NONE || board->error == ERR_GPS) {
    return board->loading ? STR_LOADING : STR_NO_DEPARTURES;
  }
  return STR_CONN_ERROR;
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index,
                                 void *context) {
  const DepartureBoard *board = model_board();
  return board->count > 0 ? board->count : 1;
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index,
                                   void *context) {
  return model_board()->count > 0 ? ROW_HEIGHT : STATUS_ROW_HEIGHT;
}

static int16_t prv_get_header_height(MenuLayer *menu_layer,
                                     uint16_t section_index, void *context) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void prv_draw_header(GContext *ctx, const Layer *cell_layer,
                            uint16_t section_index, void *context) {
  const DepartureBoard *board = model_board();
  if (board->flags & BOARD_FLAG_CACHED) {
    // Staleness beats the stop name (which the user just selected anyway)
    snprintf(s_header_text, sizeof(s_header_text), STR_OFFLINE_FMT,
             board->fetched_at);
  } else {
    snprintf(s_header_text, sizeof(s_header_text), "%s", board->stop_name);
  }
  menu_cell_basic_header_draw(ctx, cell_layer, s_header_text);
}

// Tint the departure time by realtime delay on color platforms
static GColor prv_time_color(const Departure *dep, bool highlighted) {
#if defined(PBL_COLOR)
  if (!highlighted && dep->delay != DELAY_UNKNOWN) {
    if (dep->delay > 180) {
      return GColorRed;
    }
    if (dep->delay > 60) {
      return GColorChromeYellow;
    }
    return GColorIslamicGreen;
  }
#else
  (void)dep;
#endif
  return highlighted ? GColorWhite : GColorBlack;
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer,
                         MenuIndex *cell_index, void *context) {
  const DepartureBoard *board = model_board();
  GRect bounds = layer_get_bounds(cell_layer);

  if (board->count == 0) {
    graphics_draw_text(ctx, prv_status_message(board),
                       fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(MARGIN, 2, bounds.size.w - 2 * MARGIN, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                       NULL);
    return;
  }

  const Departure *dep = &board->items[cell_index->row];
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);

  // Top row: line number (left, bold) + departure time (right, bold)
  graphics_draw_text(ctx, dep->line,
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(MARGIN, -4, LINE_BOX_W, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
  graphics_context_set_text_color(ctx, prv_time_color(dep, highlighted));
  graphics_draw_text(ctx, dep->time,
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(bounds.size.w - TIME_BOX_W - MARGIN, -4, TIME_BOX_W,
                           28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight,
                     NULL);
  graphics_context_set_text_color(ctx,
                                  highlighted ? GColorWhite : GColorBlack);
  // Bottom row: destination
  graphics_draw_text(ctx, dep->dest,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(MARGIN, 20, bounds.size.w - 2 * MARGIN, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index,
                             void *context) {
  // Open the route of the tapped departure (downstream stops)
  const DepartureBoard *board = model_board();
  if (board->count == 0) {
    return;
  }
  const Departure *dep = &board->items[cell_index->row];
  trip_window_push(dep->line, dep->dest);
}

static void prv_select_long_click(MenuLayer *menu_layer, MenuIndex *cell_index,
                                  void *context) {
  // Long-press still refreshes the current stop (the board also auto-refreshes
  // on window entry, but keep a manual gesture for power users)
  const DepartureBoard *board = model_board();
  comm_request_departures(board->stop_id, board->stop_name);
}

static void prv_board_updated(void) {
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
      .select_click = prv_select_click,
      .select_long_click = prv_select_long_click,
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

  comm_set_board_handler(prv_board_updated);
}

static void prv_window_unload(Window *window) {
  comm_set_board_handler(NULL);
  status_bar_layer_destroy(s_status_bar);
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void departures_window_push(const StopRef *stop) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = prv_window_load,
        .unload = prv_window_unload,
    });
  }
  window_stack_push(s_window, true);
  comm_request_departures(stop->id, stop->name);
}

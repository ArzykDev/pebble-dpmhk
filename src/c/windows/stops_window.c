#include "stops_window.h"

#include "../comm.h"
#include "../model.h"
#include "../strings.h"
#include "../ui_theme.h"
#include "departures_window.h"

#define SECTION_FAVORITES 0
#define SECTION_NEAREST 1
#define STOP_ROW_HEIGHT 50
#define STATUS_ROW_HEIGHT 44  // MenuLayer default (not exported by the SDK)
#define STAR_W 20

static Window *s_window;
static MenuLayer *s_menu_layer;
static StatusBarLayer *s_status_bar;

static const char *prv_nearest_status(const StopsModel *stops,
                                      uint32_t *icon) {
  *icon = RESOURCE_ID_ICON_SMALL_LOCATION;
  if (stops->nearest_loading) {
    *icon = 0;
    return STR_LOADING;
  }
  if (stops->nearest_error == ERR_GPS) {
    return STR_NO_LOCATION;
  }
  if (stops->nearest_error != ERR_NONE) {
    return theme_error_status(stops->nearest_error, true, icon);
  }
  return STR_FIND_NEAREST;
}

// Inline status row: small system icon left of the text
static void prv_draw_status(GContext *ctx, const Layer *cell_layer,
                            uint32_t icon, const char *text) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  const int icon_size = 25;
  int margin = theme_row_inset(cell_layer, (bounds.size.h - 24) / 2, 24);
  int x = margin + 2;
  if (icon) {
    theme_draw_icon(ctx, icon,
                    GPoint(x, (bounds.size.h - icon_size) / 2), highlighted);
    x += icon_size + 6;
  }
  graphics_context_set_text_color(ctx, highlighted ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(x, (bounds.size.h - 24) / 2,
                           bounds.size.w - x - margin, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
}

static uint16_t prv_get_num_sections(MenuLayer *menu_layer, void *context) {
  return 2;
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index,
                                 void *context) {
  const StopsModel *stops = model_stops();
  if (section_index == SECTION_FAVORITES) {
    return stops->favorites_count > 0 ? stops->favorites_count : 1;
  }
  return stops->nearest_count > 0 ? stops->nearest_count : 1;
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index,
                                   void *context) {
  const StopsModel *stops = model_stops();
  bool has_stop = cell_index->section == SECTION_FAVORITES
                      ? stops->favorites_count > 0
                      : stops->nearest_count > 0;
  return has_stop ? STOP_ROW_HEIGHT : STATUS_ROW_HEIGHT;
}

// Name (+ distance and favorite star on nearest rows) over the served lines
static void prv_draw_stop(GContext *ctx, const Layer *cell_layer,
                          const StopRef *stop, bool starred) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  GColor fg = highlighted ? GColorWhite : GColorBlack;
  int margin = theme_row_inset(cell_layer, 2, 24);
  int x = margin;
  int right = bounds.size.w - margin;

  if (starred) {
    theme_draw_star(ctx, GPoint(x + 8, 16), highlighted);
    x += STAR_W;
  }
  graphics_context_set_text_color(ctx, fg);
  if (stop->dist[0]) {
    GFont dist_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    GSize ds = graphics_text_layout_get_content_size(
        stop->dist, dist_font, GRect(0, 0, right - x, 24),
        GTextOverflowModeFill, GTextAlignmentRight);
    graphics_draw_text(ctx, stop->dist, dist_font,
                       GRect(right - ds.w, 6, ds.w, 24), GTextOverflowModeFill,
                       GTextAlignmentRight, NULL);
    right -= ds.w + 4;
  }
  graphics_draw_text(ctx, stop->name,
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(x, -2, right - x, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
  if (stop->lines[0]) {
    theme_draw_line_chips(ctx,
                          GRect(theme_row_inset(cell_layer, 29, 16), 29,
                                bounds.size.w -
                                    2 * theme_row_inset(cell_layer, 29, 16),
                                16),
                          stop->lines, highlighted);
  }
}

static int16_t prv_get_header_height(MenuLayer *menu_layer,
                                     uint16_t section_index, void *context) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void prv_draw_header(GContext *ctx, const Layer *cell_layer,
                            uint16_t section_index, void *context) {
  theme_draw_header(
      ctx, cell_layer,
      section_index == SECTION_FAVORITES ? STR_FAVORITES : STR_NEAREST);
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer,
                         MenuIndex *cell_index, void *context) {
  const StopsModel *stops = model_stops();
  if (cell_index->section == SECTION_FAVORITES) {
    if (stops->favorites_count == 0) {
      prv_draw_status(ctx, cell_layer, RESOURCE_ID_ICON_SMALL_QUESTION,
                      STR_NO_FAVORITES);
    } else {
      prv_draw_stop(ctx, cell_layer, &stops->favorites[cell_index->row],
                    false);
    }
  } else {
    if (stops->nearest_count == 0) {
      uint32_t icon;
      const char *text = prv_nearest_status(stops, &icon);
      prv_draw_status(ctx, cell_layer, icon, text);
    } else {
      const StopRef *stop = &stops->nearest[cell_index->row];
      prv_draw_stop(ctx, cell_layer, stop,
                    model_find_favorite(stop->name) != NULL);
    }
  }
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index,
                             void *context) {
  StopsModel *stops = model_stops();
  if (cell_index->section == SECTION_FAVORITES) {
    if (stops->favorites_count > 0) {
      departures_window_push(&stops->favorites[cell_index->row]);
    }
  } else {
    if (stops->nearest_count > 0) {
      departures_window_push(&stops->nearest[cell_index->row]);
    } else if (!stops->nearest_loading) {
      comm_request_nearest();
    }
  }
}

static void prv_stops_updated(void) {
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

  comm_set_stops_handler(prv_stops_updated);
}

static void prv_window_unload(Window *window) {
  comm_set_stops_handler(NULL);
  status_bar_layer_destroy(s_status_bar);
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void stops_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = prv_window_load,
        .unload = prv_window_unload,
    });
  }
  window_stack_push(s_window, true);
}

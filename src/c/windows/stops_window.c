#include "stops_window.h"

#include "../comm.h"
#include "../model.h"
#include "../strings.h"
#include "../ui_theme.h"
#include "departures_window.h"

#define SECTION_FAVORITES 0
#define SECTION_NEAREST 1

static Window *s_window;
static MenuLayer *s_menu_layer;
static StatusBarLayer *s_status_bar;

static const char *prv_nearest_status(const StopsModel *stops) {
  if (stops->nearest_loading) {
    return STR_LOADING;
  }
  if (stops->nearest_error == ERR_GPS) {
    return STR_NO_LOCATION;
  }
  if (stops->nearest_error != ERR_NONE) {
    return STR_CONN_ERROR;
  }
  return STR_FIND_NEAREST;
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

static int16_t prv_get_header_height(MenuLayer *menu_layer,
                                     uint16_t section_index, void *context) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void prv_draw_header(GContext *ctx, const Layer *cell_layer,
                            uint16_t section_index, void *context) {
  menu_cell_basic_header_draw(
      ctx, cell_layer,
      section_index == SECTION_FAVORITES ? STR_FAVORITES : STR_NEAREST);
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer,
                         MenuIndex *cell_index, void *context) {
  const StopsModel *stops = model_stops();
  if (cell_index->section == SECTION_FAVORITES) {
    if (stops->favorites_count == 0) {
      menu_cell_basic_draw(ctx, cell_layer, STR_NO_FAVORITES, NULL, NULL);
    } else {
      menu_cell_basic_draw(ctx, cell_layer,
                           stops->favorites[cell_index->row].name, NULL, NULL);
    }
  } else {
    if (stops->nearest_count == 0) {
      menu_cell_basic_draw(ctx, cell_layer, prv_nearest_status(stops), NULL,
                           NULL);
    } else {
      const StopRef *stop = &stops->nearest[cell_index->row];
      menu_cell_basic_draw(ctx, cell_layer, stop->name,
                           stop->dist[0] ? stop->dist : NULL, NULL);
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

static void prv_select_long_click(MenuLayer *menu_layer, MenuIndex *cell_index,
                                  void *context) {
  StopsModel *stops = model_stops();
  if (cell_index->section == SECTION_FAVORITES) {
    if (stops->favorites_count > 0) {
      vibes_short_pulse();
      comm_remove_favorite(stops->favorites[cell_index->row].id);
    }
  } else if (stops->nearest_count > 0) {
    vibes_short_pulse();
    comm_add_favorite(stops->nearest[cell_index->row].id);
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

#include "departures_window.h"

#include "../comm.h"
#include "../strings.h"
#include "../ui_theme.h"
#include "trip_window.h"

#define ROW_HEIGHT 52
#define STATUS_ROW_HEIGHT 32
#define MARGIN PBL_IF_ROUND_ELSE(18, 4)
#define LINE_BOX_W 40
#define SMALL_TIME_W 44

// Board reveal: each row slides REVEAL_TRAVEL units, later rows delayed by
// REVEAL_STAGGER. Progress must run past the last staggered row or it snaps.
#define REVEAL_TRAVEL 100
#define REVEAL_STAGGER 18
#define REVEAL_CAP_ROWS 4  // only the on-screen rows need to cascade
#define REVEAL_SPAN (REVEAL_TRAVEL + REVEAL_CAP_ROWS * REVEAL_STAGGER)

static Window *s_window;
static MenuLayer *s_menu_layer;
static StatusBarLayer *s_status_bar;
static char s_header_text[NAME_LEN + TIME_LEN + 12];

// Board reveal cascade: rows slide in from the right, later rows lag behind.
static Animation *s_reveal_anim;
static int s_reveal;  // 0..REVEAL_SPAN, REVEAL_SPAN = fully revealed
static uint8_t s_prev_count;
// Animated "Načítám" ellipsis while a fresh board is in flight.
static AppTimer *s_load_timer;
static uint8_t s_load_phase;

static void prv_mark_dirty(void) {
  if (s_menu_layer) {
    layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
  }
}

static void prv_reveal_update(Animation *anim, const AnimationProgress p) {
  s_reveal = (p * REVEAL_SPAN) / ANIMATION_NORMALIZED_MAX;
  prv_mark_dirty();
}

static void prv_reveal_stopped(Animation *anim, bool finished, void *context) {
  s_reveal = REVEAL_SPAN;
  s_reveal_anim = NULL;  // auto-destroyed by the framework
  prv_mark_dirty();
}

static const AnimationImplementation s_reveal_impl = {
    .update = prv_reveal_update,
};

static void prv_start_reveal(void) {
  if (s_reveal_anim) {
    animation_unschedule(s_reveal_anim);  // frees the previous one
    s_reveal_anim = NULL;
  }
  s_reveal = 0;
  Animation *a = animation_create();
  animation_set_implementation(a, &s_reveal_impl);
  animation_set_duration(a, 400);
  animation_set_curve(a, AnimationCurveEaseOut);
  animation_set_handlers(a, (AnimationHandlers){.stopped = prv_reveal_stopped},
                         NULL);
  s_reveal_anim = a;
  animation_schedule(a);
}

// Horizontal slide-in offset for a row given the global reveal progress. Each
// row's local progress is the shared progress minus its stagger, clamped to a
// full 0..REVEAL_TRAVEL so every row lands flush (offset 0) by the end.
static int prv_reveal_offset(int row, int width) {
  if (s_reveal >= REVEAL_SPAN) {
    return 0;
  }
  int r = row < REVEAL_CAP_ROWS ? row : REVEAL_CAP_ROWS;
  int local = s_reveal - r * REVEAL_STAGGER;
  if (local < 0) {
    local = 0;
  } else if (local > REVEAL_TRAVEL) {
    local = REVEAL_TRAVEL;
  }
  return width * (REVEAL_TRAVEL - local) / REVEAL_TRAVEL;
}

static void prv_load_tick(void *data) {
  s_load_timer = NULL;
  const DepartureBoard *board = model_board();
  if (board->count == 0 && board->loading) {
    s_load_phase = (s_load_phase + 1) % 4;
    prv_mark_dirty();
    s_load_timer = app_timer_register(400, prv_load_tick, NULL);
  }
}

static void prv_maybe_start_load_anim(const DepartureBoard *board) {
  if (board->count == 0 && board->loading && !s_load_timer) {
    s_load_phase = 0;
    s_load_timer = app_timer_register(400, prv_load_tick, NULL);
  }
}

static const char *prv_status_message(const DepartureBoard *board) {
  if (board->error == ERR_NONE || board->error == ERR_GPS) {
    if (!board->loading) {
      return STR_NO_DEPARTURES;
    }
    static char buf[16];
    snprintf(buf, sizeof(buf), "%s%.*s", STR_LOADING_BASE, s_load_phase, "...");
    return buf;
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
  int ox = prv_reveal_offset(cell_index->row, bounds.size.w);

  // Top-left: colored line badge (padded off the top edge to breathe)
  theme_draw_line_badge(ctx, GRect(MARGIN + ox, 5, LINE_BOX_W, 26), dep->line,
                        highlighted);

  // Top-right: relative countdown (leads); the absolute time follows small
  // below. Past 60 min the countdown loses its edge, so show the clock instead.
  int mins = theme_minutes_until(dep->time);
  char big[16];
  bool departed = false;
  bool show_clock = false;  // small absolute time on the bottom row
  if (mins == THEME_MIN_INVALID || mins >= 60) {
    snprintf(big, sizeof(big), "%s", dep->time);
  } else if (mins < 0) {
    departed = true;
    show_clock = true;
    snprintf(big, sizeof(big), "%s", STR_DEPARTED);
  } else if (mins == 0) {
    show_clock = true;
    snprintf(big, sizeof(big), "%s", STR_NOW);
  } else {
    show_clock = true;
    snprintf(big, sizeof(big), STR_MIN_FMT, mins);
  }

  GColor big_color = highlighted ? GColorWhite
                     : departed  ? PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
                                 : prv_time_color(dep, highlighted);
  GColor sub_color = highlighted ? GColorWhite
                     : departed  ? PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
                                 : GColorBlack;

  int right_x = MARGIN + LINE_BOX_W + 2;
  graphics_context_set_text_color(ctx, big_color);
  graphics_draw_text(ctx, big, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(right_x + ox, 1, bounds.size.w - right_x - MARGIN,
                           28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight,
                     NULL);

  // Bottom row: destination (left) + small absolute time (right)
  int dest_w = bounds.size.w - 2 * MARGIN - (show_clock ? SMALL_TIME_W : 0);
  graphics_context_set_text_color(ctx, sub_color);
  graphics_draw_text(ctx, dep->dest, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                     GRect(MARGIN + ox, 30, dest_w, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
  if (show_clock) {
    graphics_draw_text(ctx, dep->time, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(bounds.size.w - MARGIN - SMALL_TIME_W + ox, 30,
                             SMALL_TIME_W, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight,
                       NULL);
  }
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
  const DepartureBoard *board = model_board();
  // First rows of a fresh (non-offline) load just landed — cascade them in.
  if (s_prev_count == 0 && board->count > 0 &&
      !(board->flags & BOARD_FLAG_CACHED)) {
    prv_start_reveal();
  }
  s_prev_count = board->count;
  prv_maybe_start_load_anim(board);
  if (s_menu_layer) {
    menu_layer_reload_data(s_menu_layer);
  }
}

// Re-render each minute so the countdowns keep ticking (including the offline
// board, which counts down from its persisted times).
static void prv_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
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

  s_reveal = REVEAL_SPAN;
  s_prev_count = 0;
  comm_set_board_handler(prv_board_updated);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_minute_tick);
}

static void prv_window_unload(Window *window) {
  if (s_reveal_anim) {
    animation_unschedule(s_reveal_anim);
    s_reveal_anim = NULL;
  }
  if (s_load_timer) {
    app_timer_cancel(s_load_timer);
    s_load_timer = NULL;
  }
  tick_timer_service_unsubscribe();
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

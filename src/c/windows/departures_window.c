#include "departures_window.h"

#include "../comm.h"
#include "../strings.h"
#include "../ui_theme.h"
#include "trip_window.h"

#define ROW_HEIGHT 52
#define STATUS_ROW_HEIGHT 32
#define MARGIN PBL_IF_ROUND_ELSE(18, 4)
#define LINE_BOX_W 40
#define GROUP_TIMES 3  // next departures shown per line+destination row
// The API has no realtime data, so re-fetch only when the board went stale:
// its earliest departure left, or it is this many minutes old.
#define REFRESH_AFTER_MIN 5
#define FOOTER_ROW_HEIGHT 36

// Board rows, then the freshness footer (touch has no long-press, so refresh
// needs a row of its own).
#define SECTION_BOARD 0
#define SECTION_FOOTER 1

// Board reveal: each row slides REVEAL_TRAVEL units, later rows delayed by
// REVEAL_STAGGER. Progress must run past the last staggered row or it snaps.
#define REVEAL_TRAVEL 100
#define REVEAL_STAGGER 18
#define REVEAL_CAP_ROWS 4  // only the on-screen rows need to cascade
#define REVEAL_SPAN (REVEAL_TRAVEL + REVEAL_CAP_ROWS * REVEAL_STAGGER)

static Window *s_window;
static MenuLayer *s_menu_layer;
static StatusBarLayer *s_status_bar;

// Board reveal cascade: rows slide in from the right, later rows lag behind.
static Animation *s_reveal_anim;
static int s_reveal;  // 0..REVEAL_SPAN, REVEAL_SPAN = fully revealed
static uint8_t s_prev_count;
// One row per line+destination with its next departures (indices into the
// board, chronological). Rebuilt when the board changes and every minute, so
// departed times drop out.
typedef struct {
  uint8_t items[GROUP_TIMES];
  uint8_t n;
} Group;
static Group s_groups[MAX_DEPARTURES];
static uint8_t s_group_count;
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

static const char *prv_status_message(const DepartureBoard *board,
                                      uint32_t *icon) {
  if (board->error == ERR_NONE || board->error == ERR_GPS) {
    if (!board->loading) {
      *icon = RESOURCE_ID_ICON_STATUS_NO_DEPARTURES;
      return STR_NO_DEPARTURES;
    }
    static char buf[16];
    snprintf(buf, sizeof(buf), "%s%.*s", STR_LOADING_BASE, s_load_phase, "...");
    *icon = 0;
    return buf;
  }
  return theme_error_status(board->error, false, icon);
}

static void prv_regroup(void) {
  const DepartureBoard *board = model_board();
  s_group_count = 0;
  for (uint8_t i = 0; i < board->count; i++) {
    const Departure *dep = &board->items[i];
    int mins = theme_minutes_until(dep->time);
    if (mins != THEME_MIN_INVALID && mins < 0) {
      continue;  // departed
    }
    Group *g = NULL;
    for (uint8_t j = 0; j < s_group_count; j++) {
      const Departure *first = &board->items[s_groups[j].items[0]];
      if (strcmp(first->line, dep->line) == 0 &&
          strcmp(first->dest, dep->dest) == 0) {
        g = &s_groups[j];
        break;
      }
    }
    if (!g) {
      g = &s_groups[s_group_count++];
      g->n = 0;
    }
    if (g->n < GROUP_TIMES) {
      g->items[g->n++] = i;
    }
  }
}

// "4 · 19 · 34 min", "52 min · 22:51": minutes under an hour, the clock after,
// "min" once per run of minute values. Adds times only while they fit width.
static void prv_format_times(const Group *g, GFont font, int width, char *out,
                             size_t size) {
  const DepartureBoard *board = model_board();
  out[0] = '\0';
  for (uint8_t k = 0; k < g->n; k++) {
    const char *time = board->items[g->items[k]].time;
    int mins = theme_minutes_until(time);
    bool is_min = mins != THEME_MIN_INVALID && mins < 60;
    char token[16];
    if (!is_min) {
      snprintf(token, sizeof(token), "%s", time);
    } else if (mins == 0) {
      snprintf(token, sizeof(token), "%s", STR_NOW);
    } else {
      snprintf(token, sizeof(token), "%d", mins);
    }
    // "min" closes a run of minutes: before a clock time or at the very end
    bool next_is_min = false;
    if (k + 1 < g->n) {
      int next = theme_minutes_until(board->items[g->items[k + 1]].time);
      next_is_min = next != THEME_MIN_INVALID && next < 60 && next > 0;
    }
    bool add_unit = is_min && mins > 0 && !next_is_min;
    size_t len = strlen(out);
    snprintf(out + len, size - len, "%s%s%s", k ? " · " : "", token,
             add_unit ? " " STR_MIN_UNIT : "");
    if (k > 0 && graphics_text_layout_get_content_size(
                     out, font, GRect(0, 0, 1000, 30), GTextOverflowModeFill,
                     GTextAlignmentLeft)
                         .w > width) {
      out[len] = '\0';  // didn't fit: keep the times that did
      break;
    }
  }
}

static uint16_t prv_get_num_sections(MenuLayer *menu_layer, void *context) {
  return 2;
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index,
                                 void *context) {
  if (section_index == SECTION_FOOTER) {
    // Hidden mid-load: refreshing a board that is still arriving is moot
    const DepartureBoard *board = model_board();
    return board->loading && !board->silent ? 0 : 1;
  }
  return s_group_count > 0 ? s_group_count : 1;
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index,
                                   void *context) {
  if (cell_index->section == SECTION_FOOTER) {
    return FOOTER_ROW_HEIGHT;
  }
  if (s_group_count > 0) {
    return ROW_HEIGHT;
  }
  // The empty state fills the screen, leaving the Obnovit row (when shown)
  // peeking below
  GRect frame = layer_get_bounds(menu_layer_get_layer(menu_layer));
  int h = frame.size.h - MENU_CELL_BASIC_HEADER_HEIGHT -
          (model_board()->loading ? 0 : FOOTER_ROW_HEIGHT);
  return h > STATUS_ROW_HEIGHT ? h : STATUS_ROW_HEIGHT;
}

static int16_t prv_get_header_height(MenuLayer *menu_layer,
                                     uint16_t section_index, void *context) {
  return section_index == SECTION_BOARD ? MENU_CELL_BASIC_HEADER_HEIGHT : 0;
}

static void prv_draw_header(GContext *ctx, const Layer *cell_layer,
                            uint16_t section_index, void *context) {
  // Launch opens a stop the phone picks, so always say which one it is
  const DepartureBoard *board = model_board();
  menu_cell_basic_header_draw(
      ctx, cell_layer, board->stop_name[0] ? board->stop_name : STR_LOCATING);
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

// "Aktualizováno HH:MM ↻": shows how fresh the board is and refreshes on
// select/tap. The glyph is drawn (system fonts lack ↻ on some platforms).
static void prv_draw_footer(GContext *ctx, const Layer *cell_layer) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  GColor fg = highlighted ? GColorWhite : GColorBlack;
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  const DepartureBoard *board = model_board();
  char text[32];
  snprintf(text, sizeof(text),
           (board->flags & BOARD_FLAG_CACHED) ? STR_OFFLINE_FMT
                                              : STR_UPDATED_FMT,
           board->fetched_at);
  const int glyph_w = 18;
  GSize ts = graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, bounds.size.w - 2 * MARGIN - glyph_w, 24),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int x = (bounds.size.w - ts.w - glyph_w) / 2;
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, text, font,
                     GRect(x, (bounds.size.h - 24) / 2, ts.w, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);
  GPoint c = GPoint(x + ts.w + glyph_w / 2 + 2, bounds.size.h / 2 + 1);
  graphics_context_set_stroke_color(ctx, fg);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_arc(ctx, GRect(c.x - 6, c.y - 6, 13, 13), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(60), DEG_TO_TRIGANGLE(360));
  // Arrowhead at the arc's open end (top)
  graphics_context_set_fill_color(ctx, fg);
  GPathInfo head = {.num_points = 3,
                    .points = (GPoint[]){{c.x + 1, c.y - 9}, {c.x + 6, c.y - 6},
                                         {c.x + 1, c.y - 2}}};
  GPath *p = gpath_create(&head);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer,
                         MenuIndex *cell_index, void *context) {
  if (cell_index->section == SECTION_FOOTER) {
    prv_draw_footer(ctx, cell_layer);
    return;
  }
  const DepartureBoard *board = model_board();
  GRect bounds = layer_get_bounds(cell_layer);

  if (s_group_count == 0) {
    uint32_t icon;
    const char *text = prv_status_message(board, &icon);
    theme_draw_status(ctx, bounds, icon, text);
    return;
  }

  const Group *g = &s_groups[cell_index->row];
  const Departure *first = &board->items[g->items[0]];
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  GColor fg = highlighted ? GColorWhite : GColorBlack;
  int ox = prv_reveal_offset(cell_index->row, bounds.size.w);
  int w = bounds.size.w - 2 * MARGIN;

  // Top: line badge, then the destination beside it
  theme_draw_line_badge(ctx, GRect(MARGIN + ox, 3, LINE_BOX_W, 26), first->line,
                        highlighted);
  int dest_x = MARGIN + LINE_BOX_W + 4;
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, first->dest,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(dest_x + ox, 4, bounds.size.w - dest_x - MARGIN, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                     NULL);

  // Bottom: the next departures, right-aligned and leading the eye
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  char times[48];
  prv_format_times(g, font, w, times, sizeof(times));
  graphics_context_set_text_color(ctx, prv_time_color(first, highlighted));
  graphics_draw_text(ctx, times, font, GRect(MARGIN + ox, 22, w, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight,
                     NULL);
}

static void prv_refresh(void) {
  const DepartureBoard *board = model_board();
  comm_request_departures(board->stop_id, board->stop_name);
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index,
                             void *context) {
  const DepartureBoard *board = model_board();
  if (cell_index->section == SECTION_FOOTER) {
    prv_refresh();
    return;
  }
  // Open the route of the tapped line (downstream stops)
  if (s_group_count == 0) {
    return;
  }
  const Departure *dep = &board->items[s_groups[cell_index->row].items[0]];
  trip_window_push(dep->line, dep->dest);
}

static void prv_select_long_click(MenuLayer *menu_layer, MenuIndex *cell_index,
                                  void *context) {
  // Button shortcut for the footer's refresh
  prv_refresh();
}

static void prv_board_updated(void) {
  const DepartureBoard *board = model_board();
  prv_regroup();
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
static bool prv_board_stale(const DepartureBoard *board) {
  if (board->loading || board->count == 0 || !board->stop_id[0] ||
      (board->flags & BOARD_FLAG_CACHED)) {
    return false;  // offline boards refresh only on request (footer tap)
  }
  int first = theme_minutes_until(board->items[0].time);
  if (first != THEME_MIN_INVALID && first < 0) {
    return true;
  }
  // Minutes until fetched_at: negative = its age; positive = wrapped past
  // the next-day window, i.e. very old
  int since = theme_minutes_until(board->fetched_at);
  return since == THEME_MIN_INVALID || since <= -REFRESH_AFTER_MIN || since > 0;
}

static void prv_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  if (prv_board_stale(model_board())) {
    comm_refresh_departures();
  }
  prv_regroup();
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
  s_group_count = 0;
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

static void prv_push(bool animated) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = prv_window_load,
        .unload = prv_window_unload,
    });
  }
  window_stack_push(s_window, animated);
}

void departures_window_push(const StopRef *stop) {
  prv_push(true);
  comm_request_departures(stop->id, stop->name);
}

void departures_window_push_auto(void) {
  prv_push(false);
  comm_request_departures("", "");
}

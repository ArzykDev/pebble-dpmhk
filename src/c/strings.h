#pragma once

// Czech UI strings (UTF-8). Keep all user-visible text here.
#define STR_APP_TITLE     "MHD HK"
#define STR_STOPS_TITLE   "Zastávky"
#define STR_FAVORITES     "Oblíbené"
#define STR_NEAREST       "Nejbližší"
#define STR_FIND_NEAREST  "Najít polohou..."
#define STR_LOADING       "Načítám..."
#define STR_LOADING_BASE  "Načítám"
#define STR_NOW           "teď"
#define STR_MIN_UNIT      "min"
#define STR_NO_DEPARTURES "Žádné spoje"
#define STR_TRIP_EMPTY    "Trasa nedostupná"
#define STR_CONN_ERROR    "Chyba spojení"
#define STR_NO_PHONE      "Telefon odpojen"
#define STR_NO_FAVORITES  "Nastavte v telefonu"
#define STR_NO_LOCATION   "Poloha nedostupná"
#define STR_OFFLINE_FMT   "Offline (%s)"
#define STR_UPDATED_FMT   "Aktualizováno %s"
#define STR_LOCATING      "Hledám zastávku…"
#define STR_REMIND_FMT    "Připomenout %s"
#define STR_REMIND_SOON_FMT "Odjíždí v %s"
#define STR_REMIND_CANCEL "Zrušit připomenutí"
#if defined(PBL_PLATFORM_APLITE)
#define STR_TRIP_ARROW    ">"  // aplite's system font lacks the arrow glyph
#else
#define STR_TRIP_ARROW    "→"
#endif

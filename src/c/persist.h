#pragma once
#include "model.h"

// Load favorites mirror from watch storage into the stops model
void persist_load_favorites(void);

// Store the stops model's favorites into watch storage
void persist_store_favorites(void);

// Store a compact copy of the current (fresh) departure board
void persist_store_board(void);

// If a board for stop_id is stored, load it into the model (flagged cached).
// Returns true when the board was loaded.
bool persist_load_board(const char *stop_id);

#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <stdbool.h>

#include "todo/settings.h"

/* The saved settings from the flash: true only for a good record (todo_settings_decode), else `out` is left alone. */
bool settings_store_load(todo_settings *out);

/* Writes them, unless the flash already holds exactly this record. Returns true if something was written. */
bool settings_store_save(const todo_settings *s);

#endif /* SETTINGS_STORE_H */

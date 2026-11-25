#ifndef TODO_BOARD_UI_H
#define TODO_BOARD_UI_H

#include "cydui/ui.h"

/* A UI over the board's budget: 320 commands, 1.5 KB of text, 64 state entries, 20-row strips. One at a time. */
uint32_t board_ui_init(cydui_ui *ui, const cydui_backend *backend, const cydui_theme *theme);

#endif /* TODO_BOARD_UI_H */

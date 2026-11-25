#include "board_ui.h"

#include <string.h>

CYDUI_STORAGE(board, 320, 240, 20, 320, 1536, 64);

uint32_t board_ui_init(cydui_ui *ui, const cydui_backend *backend, const cydui_theme *theme)
{
    memset(&board, 0, sizeof(board));
    return cydui_ui_init(ui, &CYDUI_CONFIG(board, backend, theme));
}

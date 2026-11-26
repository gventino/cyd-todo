#ifndef TODO_QR_H
#define TODO_QR_H

/*
 * QR codes for the screen. The encoder is Nayuki's (vendor/); this wraps it: error correction medium (raised when
 * there is room), versions 1..5, drawn as one paper rectangle and one 1-bit bitmap at scale 4 (CYDUI_BITMAP_A1) with a 4 module quiet
 * zone. Always dark on light, in every palette: many scanners do not read an inverted code.
 *
 * The caller owns the todo_qr (under 400 bytes: the mask is one bit per module, drawn by the library at a scale of 4
 * pixels). The library hashes a bitmap by its pointer, so a new code in the same buffer must be announced with
 * cydui_invalidate(): todo_qr_set() does it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cydui/ui.h"

#define TODO_QR_MAX_VERSION 5
#define TODO_QR_MODULE_PX 4
#define TODO_QR_QUIET 4 /* modules of empty margin on every side */
#define TODO_QR_MAX_MODULES (TODO_QR_MAX_VERSION * 4 + 17)
#define TODO_QR_MAX_PX ((TODO_QR_MAX_MODULES + 2 * TODO_QR_QUIET) * TODO_QR_MODULE_PX)
#define TODO_QR_MASK_BYTES (((TODO_QR_MAX_MODULES + 7) / 8) * TODO_QR_MAX_MODULES) /* A1: a bit per module, rows padded to a byte */
#define TODO_QR_MATRIX_BYTES (((TODO_QR_MAX_MODULES * TODO_QR_MAX_MODULES) + 7) / 8 + 1)

#define TODO_QR_PAPER CYDUI_RGB(0xF8, 0xEF, 0xD3)
#define TODO_QR_INK CYDUI_RGB(0x1A, 0x12, 0x0A)

typedef struct {
    uint8_t mask[TODO_QR_MASK_BYTES]; /* 1 bit per module, most significant bit first, rows padded to a byte: 1 for a dark module */
    uint8_t modules[TODO_QR_MATRIX_BYTES]; /* the encoder's own matrix, kept for the tests */
    int size;                         /* modules per side, 0 while there is no code */
    int px;                           /* pixels per side, with the quiet zone */
} todo_qr;

/* Encodes `text` (NUL terminated) and renders the mask. False (and `q` empty) if it does not fit version 5. */
bool todo_qr_encode(todo_qr *q, const char *text);

/* The same, for a code that is already on screen: announces the change to the library when the content differs. */
bool todo_qr_set(todo_qr *q, cydui_ui *ui, const char *text);

/* Draws the code with its top-left corner at (x, y): the paper (quiet zone included) and the bitmap. 2 commands. */
void todo_qr_draw(cydui_ui *ui, const todo_qr *q, int x, int y);

/* Whether the module at (col, row) of the matrix is dark; false outside it. */
bool todo_qr_module(const todo_qr *q, int col, int row);

/*
 * `WIFI:T:WPA;S:<ssid>;P:<password>;;` with the characters that the format reserves (\ ; , : ") escaped.
 * False if the result does not fit `cap` (including the NUL) or an argument is missing.
 */
bool todo_qr_wifi_text(const char *ssid, const char *password, char *out, size_t cap);

#endif /* TODO_QR_H */

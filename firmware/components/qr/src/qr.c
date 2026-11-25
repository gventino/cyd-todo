#include "todo/qr.h"

#include <string.h>

#include "qrcodegen.h"

static void clear(todo_qr *q)
{
    q->size = 0;
    q->px = 0;
}

static void render(todo_qr *q)
{
    const int px = (q->size + 2 * TODO_QR_QUIET) * TODO_QR_MODULE_PX;
    int row, col, dy;

    q->px = px;
    memset(q->mask, 0, (size_t)px * (size_t)px / 2);
    for (row = 0; row < q->size; row++) {
        for (col = 0; col < q->size; col++) {
            if (!qrcodegen_getModule(q->modules, col, row)) {
                continue;
            }
            for (dy = 0; dy < TODO_QR_MODULE_PX; dy++) {
                /* 4 pixels = 2 bytes, and the module starts on an even pixel */
                size_t at = ((size_t)((row + TODO_QR_QUIET) * TODO_QR_MODULE_PX + dy) * (size_t)px +
                             (size_t)((col + TODO_QR_QUIET) * TODO_QR_MODULE_PX)) / 2;
                q->mask[at] = 0xFF;
                q->mask[at + 1] = 0xFF;
            }
        }
    }
}

bool todo_qr_encode(todo_qr *q, const char *text)
{
    uint8_t temp[TODO_QR_MATRIX_BYTES];

    if (!q) {
        return false;
    }
    clear(q);
    if (!text || !qrcodegen_encodeText(text, temp, q->modules, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, TODO_QR_MAX_VERSION,
                                       qrcodegen_Mask_AUTO, true)) {
        return false;
    }
    q->size = qrcodegen_getSize(q->modules);
    render(q);
    return true;
}

bool todo_qr_set(todo_qr *q, cydui_ui *ui, const char *text)
{
    uint8_t old[TODO_QR_MATRIX_BYTES];
    int old_size;
    bool ok;

    if (!q || !ui) {
        return false;
    }
    memcpy(old, q->modules, sizeof(old));
    old_size = q->size;
    ok = todo_qr_encode(q, text);
    if (q->size != old_size || memcmp(old, q->modules, sizeof(old)) != 0) {
        cydui_invalidate(&ui->ctx); /* the bitmap pointer is the same, the pixels are not */
    }
    return ok;
}

void todo_qr_draw(cydui_ui *ui, const todo_qr *q, int x, int y)
{
    cydui_rect r;

    if (!ui || !q || q->size == 0) {
        return;
    }
    r = (cydui_rect){(int16_t)x, (int16_t)y, (int16_t)q->px, (int16_t)q->px};
    cydui_draw_rect(&ui->ctx, r, TODO_QR_PAPER, 255);
    cydui_draw_bitmap(&ui->ctx, r, q->mask, (size_t)q->px * (size_t)q->px / 2, CYDUI_BITMAP_A4, TODO_QR_INK, 255);
}

bool todo_qr_module(const todo_qr *q, int col, int row)
{
    return q && q->size > 0 && col >= 0 && row >= 0 && col < q->size && row < q->size && qrcodegen_getModule(q->modules, col, row);
}

/* Appends `s` to out[*n], escaping what the WIFI: format reserves. False if it does not fit (a NUL is always kept free). */
static bool put(char *out, size_t cap, size_t *n, const char *s, bool escape)
{
    for (; *s; s++) {
        bool special = escape && (*s == '\\' || *s == ';' || *s == ',' || *s == ':' || *s == '"');
        if (*n + (special ? 2 : 1) + 1 > cap) {
            return false;
        }
        if (special) {
            out[(*n)++] = '\\';
        }
        out[(*n)++] = *s;
    }
    return true;
}

bool todo_qr_wifi_text(const char *ssid, const char *password, char *out, size_t cap)
{
    size_t n = 0;

    if (!ssid || !password || !out || cap == 0) {
        return false;
    }
    if (!put(out, cap, &n, "WIFI:T:WPA;S:", false) || !put(out, cap, &n, ssid, true) || !put(out, cap, &n, ";P:", false) ||
        !put(out, cap, &n, password, true) || !put(out, cap, &n, ";;", false)) {
        out[0] = '\0';
        return false;
    }
    out[n] = '\0';
    return true;
}

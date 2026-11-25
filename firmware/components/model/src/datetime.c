#include "todo/datetime.h"

#include <stdio.h>

int64_t todo_days_from_civil(int y, int m, int d)
{
    int64_t yy = m <= 2 ? y - 1 : y;
    int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    int64_t yoe = yy - era * 400;
    int64_t mp = (m + 9) % 12;
    int64_t doy = (153 * mp + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void todo_civil_from_days(int64_t days, int *y, int *m, int *d)
{
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t dd = doy - (153 * mp + 2) / 5 + 1;
    int64_t mm = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(mm <= 2 ? yy + 1 : yy);
    *m = (int)mm;
    *d = (int)dd;
}

int todo_days_in_month(int y, int m)
{
    static const int len[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) {
        return 0;
    }
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
        return 29;
    }
    return len[m - 1];
}

int todo_weekday(int64_t days)
{
    /* 1970-01-01 was a Thursday (3 with Monday 0) */
    int64_t w = (days + 3) % 7;
    return (int)(w < 0 ? w + 7 : w);
}

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

void todo_local_fields(int64_t epoch, int off_min, todo_local *out)
{
    int64_t local = epoch + (int64_t)off_min * 60;
    int64_t days = floor_div(local, TODO_DAY);
    int64_t secs = local - days * TODO_DAY;

    todo_civil_from_days(days, &out->year, &out->month, &out->day);
    out->hour = (int)(secs / 3600);
    out->minute = (int)(secs % 3600 / 60);
    out->weekday = todo_weekday(days);
}

int64_t todo_local_midnight(int y, int m, int d, int off_min)
{
    return todo_days_from_civil(y, m, d) * TODO_DAY - (int64_t)off_min * 60;
}

void todo_month_prev(int *y, int *m)
{
    if (*m == 1) {
        *m = 12;
        (*y)--;
    } else {
        (*m)--;
    }
}

void todo_month_next(int *y, int *m)
{
    if (*m == 12) {
        *m = 1;
        (*y)++;
    } else {
        (*m)++;
    }
}

void todo_month_window(int y, int m, int off_min, int64_t *from, int64_t *to)
{
    int ny = y, nm = m;

    todo_month_next(&ny, &nm);
    *from = todo_local_midnight(y, m, 1, off_min);
    *to = todo_local_midnight(ny, nm, 1, off_min);
}

int todo_grid_day(int y, int m, int row, int col)
{
    int first = todo_weekday(todo_days_from_civil(y, m, 1));
    int n = row * 7 + col - first + 1;

    if (row < 0 || row > 5 || col < 0 || col > 6 || n < 1 || n > todo_days_in_month(y, m)) {
        return 0;
    }
    return n;
}

int32_t todo_due_pack(int y, int m, int d)
{
    return y * 10000 + m * 100 + d;
}

void todo_due_unpack(int32_t due, int *y, int *m, int *d)
{
    *y = due / 10000;
    *m = due / 100 % 100;
    *d = due % 100;
}

static bool digits(const char *s, int n, int *out)
{
    int v = 0, i;

    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

int32_t todo_due_parse(const char *s)
{
    int y, m, d;
    size_t n = 0;

    while (s && n < 11 && s[n]) {
        n++; /* the length first: nothing past the end is read */
    }
    if (!s || n != 10 || s[4] != '-' || s[7] != '-') {
        return 0;
    }
    if (!digits(s, 4, &y) || !digits(s + 5, 2, &m) || !digits(s + 8, 2, &d)) {
        return 0;
    }
    if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1 || d > todo_days_in_month(y, m)) {
        return 0;
    }
    return todo_due_pack(y, m, d);
}

const char *todo_month_name(int m)
{
    static const char *const names[12] = {"January", "February", "March", "April", "May", "June",
                                          "July", "August", "September", "October", "November", "December"};
    return m >= 1 && m <= 12 ? names[m - 1] : "?";
}

const char *todo_weekday_short(int wd)
{
    static const char *const names[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return wd >= 0 && wd <= 6 ? names[wd] : "?";
}

size_t todo_format_hhmm(char *buf, size_t cap, int64_t epoch, int off_min)
{
    todo_local t;
    int n;

    if (!buf || cap < 6) {
        return 0;
    }
    todo_local_fields(epoch, off_min, &t);
    n = snprintf(buf, cap, "%02d:%02d", t.hour, t.minute);
    return n > 0 ? (size_t)n : 0;
}

size_t todo_format_local(char *buf, size_t cap, int64_t epoch, int off_min)
{
    todo_local t;
    int n;

    if (!buf || cap < 17) {
        return 0;
    }
    todo_local_fields(epoch, off_min, &t);
    n = snprintf(buf, cap, "%04d-%02d-%02dT%02d:%02d", t.year, t.month, t.day, t.hour, t.minute);
    return n > 0 ? (size_t)n : 0;
}

bool todo_parse_local(const char *s, int off_min, int64_t *epoch)
{
    size_t len = 0;
    int y, m, d, hh, mm, ss = 0;

    while (s && len < 20 && s[len]) {
        len++;
    }
    if (!s || (len != 16 && len != 19) || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':') {
        return false;
    }
    if (!digits(s, 4, &y) || !digits(s + 5, 2, &m) || !digits(s + 8, 2, &d) || !digits(s + 11, 2, &hh) ||
        !digits(s + 14, 2, &mm)) {
        return false;
    }
    if (len == 19 && (s[16] != ':' || !digits(s + 17, 2, &ss))) {
        return false;
    }
    if (m < 1 || m > 12 || d < 1 || d > todo_days_in_month(y, m) || hh > 23 || mm > 59 || ss > 59) {
        return false;
    }
    *epoch = todo_days_from_civil(y, m, d) * TODO_DAY + hh * 3600 + mm * 60 + ss - (int64_t)off_min * 60;
    return true;
}

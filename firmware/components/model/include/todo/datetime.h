#ifndef TODO_DATETIME_H
#define TODO_DATETIME_H

/*
 * Calendar arithmetic in integers. Times are UTC epoch seconds (int64: 2038 is not far away); the screens
 * show them in one fixed offset from UTC (`off_min`, minutes east). The server does the same sums in Rust and
 * both are checked against one table of vectors (docs/vectors/time.tsv).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TODO_DAY 86400

typedef struct {
    int year, month, day;   /* 1-based month and day */
    int hour, minute;
    int weekday;            /* Monday 0 .. Sunday 6 */
} todo_local;

int64_t todo_days_from_civil(int y, int m, int d);
void todo_civil_from_days(int64_t days, int *y, int *m, int *d);
int todo_days_in_month(int y, int m);
/* Monday 0 .. Sunday 6, for a day count since 1970-01-01. */
int todo_weekday(int64_t days);

/* An instant, seen in a zone. */
void todo_local_fields(int64_t epoch, int off_min, todo_local *out);
/* The UTC instant of 00:00 of a local date. */
int64_t todo_local_midnight(int y, int m, int d, int off_min);
/* The UTC window [from, to) of a local month. */
void todo_month_window(int y, int m, int off_min, int64_t *from, int64_t *to);
void todo_month_prev(int *y, int *m);
void todo_month_next(int *y, int *m);

/* Which cell of a month grid (Monday first, 6 rows of 7) holds a day: the day number 1..31, or 0 for a blank. */
int todo_grid_day(int y, int m, int row, int col);

/* A due date packed as YYYYMMDD. */
int32_t todo_due_pack(int y, int m, int d);
void todo_due_unpack(int32_t due, int *y, int *m, int *d);
/* Reads "YYYY-MM-DD" (a real calendar date, 2000..2099); 0 if it is not one. */
int32_t todo_due_parse(const char *s);

/* Names, 1-based month and Monday-first weekday. Never NULL; out of range gives "?". */
const char *todo_month_name(int m);
const char *todo_weekday_short(int wd);

/* "HH:MM" and "YYYY-MM-DDTHH:MM" of an instant in a zone, and the inverse of the second. Return the length, or 0 / false. */
size_t todo_format_hhmm(char *buf, size_t cap, int64_t epoch, int off_min);
size_t todo_format_local(char *buf, size_t cap, int64_t epoch, int off_min);
bool todo_parse_local(const char *s, int off_min, int64_t *epoch);

#endif /* TODO_DATETIME_H */

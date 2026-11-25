#ifndef TODO_COORDS_H
#define TODO_COORDS_H

/* Where the things of the 320x240 screens are, for tests that touch them (the layout is in components/ui). */

/* the tab bar */
#define TAB_Y 16
#define TAB_QUESTS_X 53
#define TAB_CALENDAR_X 160
#define TAB_SETTINGS_X 267

/* the Quests tab: the first card, and the button that asks for a new quest */
#define CHECK_X 20
#define TITLE_X 120
#define CARD0_Y 82
#define NEW_QUEST_X 264
#define NEW_QUEST_Y 48

/* the calendar: arrows in the header; the grid starts at x 2, y 78 with cells of 45 x 26 (22 with six rows) */
#define CAL_PREV_X 28
#define CAL_NEXT_X 292
#define CAL_ARROW_Y 48
/* a day of a month whose first day falls on column `first` (0 = Monday) */
static inline int cell_x(int first, int d) { return 2 + ((first + d - 1) % 7) * 45 + 22; }
static inline int cell_y(int first, int d) { return 78 + ((first + d - 1) / 7) * 26 + 13; }
/* December 2026 begins on a Tuesday */
static inline int day_x(int d) { return cell_x(1, d); }
static inline int day_y(int d) { return cell_y(1, d); }

/* the agenda popup */
#define AGENDA_ROW0_X 160
#define AGENDA_ROW0_Y 56
#define AGENDA_ADD_X 79
#define AGENDA_CLOSE_X 202
#define AGENDA_BUTTONS_Y 203

/* the detail popup and the confirmation */
#define DETAIL_DELETE_X 77
#define DETAIL_CLOSE_X 190
#define DETAIL_BUTTONS_Y 178
#define CONFIRM_CANCEL_X 88
#define CONFIRM_DELETE_X 201
#define CONFIRM_BUTTONS_Y 139

/* Settings: the rows are 30 px from y 35; arrows at the right (next) and in the middle (previous) */
#define SET_NEXT_X 294
#define SET_PREV_X 158
#define SET_ZONE_Y 110
#define SET_BACKLIGHT_Y 140
#define SET_THEME_Y 170
#define SET_WIFI_Y 200
#define CONFIRM_WIFI_CANCEL_X 80
#define CONFIRM_WIFI_CONTINUE_X 202
#define CONFIRM_WIFI_Y 133

/* the QR and setup pages: the buttons are in the right column */
#define QR_BUTTON_X 242
#define QR_BUTTON_Y 130
#define QR_RETRY_Y 82
#define SETUP_CANCEL_Y 152 /* without a reason line */
#define TRY_AGAIN_X 160
#define TRY_AGAIN_Y 146

#endif /* TODO_COORDS_H */

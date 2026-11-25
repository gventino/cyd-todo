#ifndef TODO_LEVELS_H
#define TODO_LEVELS_H

/* XP and levels, integers only: the XP needed to reach level L is 50 * L * (L - 1) (1 at 0, 2 at 100, 3 at 300, ...). */

#include <stdint.h>

#define TODO_LEVEL_MAX 99
#define TODO_FRACTION_ONE 4096 /* the same fixed point as CYDUI_ANIM_ONE */

/* XP at which a level starts. */
int32_t todo_xp_for_level(int level);
/* The level of a total XP (1 for 0 or less, TODO_LEVEL_MAX at most). */
int todo_level_for_xp(int32_t xp);
/* How far through the current level, 0 .. TODO_FRACTION_ONE (full at the top level). */
int todo_level_fraction(int32_t xp);
/* XP gained by completing a quest of this difficulty. */
int32_t todo_xp_for_quest(int difficulty);

#endif /* TODO_LEVELS_H */

#include "todo/levels.h"

int32_t todo_xp_for_level(int level)
{
    if (level < 1) {
        level = 1;
    }
    if (level > TODO_LEVEL_MAX + 1) {
        level = TODO_LEVEL_MAX + 1;
    }
    return 50 * level * (level - 1);
}

int todo_level_for_xp(int32_t xp)
{
    int level = 1;

    while (level < TODO_LEVEL_MAX && xp >= todo_xp_for_level(level + 1)) {
        level++;
    }
    return level;
}

int todo_level_fraction(int32_t xp)
{
    int level = todo_level_for_xp(xp);
    int32_t start, end;

    if (level >= TODO_LEVEL_MAX) {
        return TODO_FRACTION_ONE;
    }
    start = todo_xp_for_level(level);
    end = todo_xp_for_level(level + 1);
    if (xp < start) {
        return 0;
    }
    return (int)((int64_t)(xp - start) * TODO_FRACTION_ONE / (end - start));
}

int32_t todo_xp_for_quest(int difficulty)
{
    if (difficulty < 1) {
        difficulty = 1;
    }
    if (difficulty > 5) {
        difficulty = 5;
    }
    return difficulty * 10;
}

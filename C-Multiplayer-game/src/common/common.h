#ifndef _COMMON_
#define _COMMON_

#include <error.h>
#include <errno.h>
#include <stdint.h>

#define handle_error(msg) \
           do { perror(msg); strerror(errno); exit(EXIT_FAILURE); } while (0)

 #define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define NUM_OF_DIRECTIONS 4

#define member_size(type, member) sizeof(((type *)0)->member)

#define EMPTY_SYM ' '
#define MONSTER_SYM '#'
#define WALL_SYM '='
#define COIN_SYM '$'

typedef enum Move
{
    UP,
    DOWN,
    LEFT,
    RIGHT,
    INVALID_MOVE,
    STOPPED
} Direction;

typedef struct Dimensions
{
    uint32_t height;
    uint32_t width;
} Dimensions;


#define MAX_NAME_LENGTH 50

#endif // _COMMON_

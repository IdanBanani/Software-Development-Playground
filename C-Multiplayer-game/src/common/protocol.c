#include "common.h"


unsigned char encodeDirection(Direction d)
{
    switch (d)
    {
    case UP:
        return 0x00;
    case DOWN:
        return 0x0F;
    case LEFT:
        return 0xF0;
    case RIGHT:
        return 0xFF;
    default:
        return 0xAA;
    }
}


Direction decodeDirection(unsigned char *command)
{
    switch (*command)
    {
    case 0x00:
        return UP;
    case 0x0F:
        return DOWN;
    case 0xF0:
        return LEFT;
    case 0xFF:
        return RIGHT;
    default:
        return INVALID_MOVE;
    }
}
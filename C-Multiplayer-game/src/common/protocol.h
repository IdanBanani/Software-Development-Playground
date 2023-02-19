#if !defined(PROTOCOL_H)
#define PROTOCOL_H

#include "common.h"

#define CONTROL_CMD_SIZE 1 //in bytes
#define MOVE_CMD_SIZE 1 //in bytes
// #define  0xF9
// const char I_NEED_YOUR_NAME_MSG = 0x88; //does int conversion...

//TODO: change all char comparisions to memcmp()!
const char I_NEED_YOUR_NAME_MSG = '\x88';
const char ENTER_YOUR_CHOICE_MSG = '\x99';

const char SENDING_GAME_CONFIG = '\x77';

const char GAME_STARTED = '\xFE';
const char CLIENT_ACK_GAME_STARTED = '\xCC';
const char GAME_OVER = '\xEF';

const char UPDATE_START = '\xF9';
const char UPDATE_END = '\x9F';

unsigned char encodeDirection(Direction d);
Direction decodeDirection(unsigned char *command);


#endif // PROTOCOL_H

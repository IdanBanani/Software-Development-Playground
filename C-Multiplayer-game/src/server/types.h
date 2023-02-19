#ifndef _MY_TYPES
#define _MY_TYPES

#include "../common/common.h"
#include <stdint.h>


// TODO: create typedefs enums for error codes
#define COIN_WORTH 10
#define BOARD_WIDTH 20
#define BOARD_HEIGHT 15
#define LISTEN_PORT 3771
#define IP_MAX_CHAR_LEN 16
#define MAX_NUM_OF_PLAYERS 26
#define MAX_EVENTS ((MAX_NUM_OF_PLAYERS) + 0)
#define MAX_PLAYER_NAME 30
#define IT_IS_A_NEW_PLAYER -1
#define MAX_COMMAND_LEN MAX_PLAYER_NAME
#define MAX_OPTION_LEN 1
#define MOVE_COMMNAD_LEN 1
#define SMALL_MSG_MAX_LEN 200
#define UPDATES_INTERVAL_USECONDS 0.5
#define TRY_AGAIN_LATER 666
#define MIN_OPTION 1
#define MAX_OPTION 3

typedef enum Operator{
    Increment,
    Decrement
} Operator ;

typedef enum ClientState
{
    DISCONNECTED,
    JUST_LOGGED_IN,
    WAS_ASKED_FOR_NAME,
    WAITING_FOR_NAME,
    RECEIVED_NAME,
    WAITING_FOR_OPTIONS,
    WAS_SHOWN_OPTIONS,
    WAITING_FOR_CHOICE,
    WAITING_FOR_OTHERS,
    WAITING_FOR_GAME_TO_START,
    WAITING_FOR_CLIENT_TO_ACK_GAME,
    INSIDE_A_GAME,
    SPECTATOR,
    DEAD,
} Player_State;

typedef enum Option
{
    INVALID_OPT,
    START_NEW_GAME,
    JOIN_EXISTING_GAME,
    WAIT_FOR_OTHERS
} Choice;

typedef struct Location{
    uint32_t row;
    uint32_t col;
} Location;


typedef struct Player{
    Location location;
    Player_State state;
    unsigned int points;
    char name[MAX_PLAYER_NAME];
    bool hasSuperpower;
    int index;
} Player;


typedef struct Monster{
    Location location;
    uint32_t index;
} Monster;


typedef struct Obstacle{   
    int hp;
} Obstacle;
typedef struct Coin{
    int hp;
} Coin;
typedef struct Bonus{
    int hp;
} Bonus;

typedef struct Empty{
    int hp;
} Empty;


typedef union GameObjectType
{
    Monster *m;
    Player *p;
    Obstacle o;
    Coin *c;
    Bonus *b;
    Empty *e;
} GameObj;

typedef struct Cell {
    bool isEmpty;
    bool containsCoin;
    char symbol;
    GameObj obj; //TODO: Should it be a pointer? , should we just swap the underlying pointers when moving a
                // charactar on board? 
} Cell ;
 
typedef struct Board {
    //TODO: change to dynamic size?
    //TODO: use cells instead of poiters to cells?
    Cell cells[BOARD_HEIGHT][BOARD_WIDTH];
    Dimensions dimensions;
} Board, *pBoard;



#endif // _MY_TYPES

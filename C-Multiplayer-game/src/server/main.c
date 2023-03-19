#include <errno.h>
#include <error.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <sys/random.h>
#include <pthread.h>
#include "../common/protocol.h"
#include "../common/common.h"
#include "socket_api.h"
#include "types.h"

#define RAND_BYTES_BUF_LEN 25

#define SMALL_TV_USEC 30
#define EPOLL_TIMEOUT_MSEC 30
#define NUM_OF_MONSTERS_DIVIDER 100 
#define NUM_OF_MONSTERS_MODULO 4
#define INTERVAL_TV_USEC 150000
// #define TV_TV_USEC 500000

#define MAX_NUM_OF_MONSTERS ((BOARD_HEIGHT * BOARD_WIDTH) / 2)
#define MONSTER_TRIES_TRESHOLD 80
#define SUPERPOWER_DIVIDER 50
#define NUM_OF_ROUNDS_FOR_SUPERPOWER 100 
#define SUPERPOWER_SPEED 2
#define UPDATER_THREAD_EXITED 444
#define MONSTER_DIED 202
static pBoard p_board = NULL;
static unsigned int num_of_coins_on_screen = 0;

static Cell emptyCell = {.isEmpty = true, .containsCoin = false, .obj = {0}, .symbol = EMPTY_SYM};
static Cell wallCell = {.isEmpty = false, .containsCoin = false, .obj = {0}, .symbol = WALL_SYM};
static Cell coinCell = {.isEmpty = false, .containsCoin = true, .obj = {0}, .symbol = COIN_SYM};

static int game_initiator_idx = -1;
static int num_of_monsters = 0;

static bool has_someone_started_a_game = false;


// TODO: Does this really need to be global?
static struct epoll_event ev, events[MAX_EVENTS];
static int listen_sock, nfds, epollfd;
//////////////////////////

// TODO: replace with doubly linked list
static int num_of_known_players = 0;
static unsigned int num_of_alive_players = 0;
static int known_players_fds[MAX_NUM_OF_PLAYERS];
static long long unsigned int superpower_timers[MAX_NUM_OF_PLAYERS] = {0};
static Direction last_move_commands[MAX_NUM_OF_PLAYERS];
static Player players_info[MAX_NUM_OF_PLAYERS];
static Monster *monsters[MAX_NUM_OF_MONSTERS] = {0};

static pthread_t updates_sender_thread;


static void *set_timer_signal(void *blocked_signals_set);


static char current_player_symbol = 'a';
static int displayIP();
static int getPlayerIndex(int player_fd);

inline static bool isBoundary(uint32_t r, uint32_t c, uint32_t num_of_rows, uint32_t num_of_cols);
static void newMonster(uint32_t i, uint32_t j);
static int init_board();
static void create_symbolized_board(char *symbols_board);
static void set_timer_helper(suseconds_t tv_tv_usec, suseconds_t interval_tv_usec);
static int handle_disconnected_player(int player_fd, int player_idx);
int moveMonster(Cell *monster_cell);
static void check_for_clients_moves();
static void timer_handler();
static int start_game();
int adjustDecrement(uint32_t *x, uint32_t divisor);
int calculateNextLocation(Location *l, Direction dir);
static Direction random_direction_generator();
static void mark_as_dead(int player_idx);
static bool shouldGetSuperPower(unsigned int points);
inline static Cell *getCellAtLocation(Location *l);
static void updateSuperPowers();
static void handle_player_move(Player *player, Direction d);
static int handle_monster_move(Monster *monster, Direction d);
static int setnonblocking(int socket_fd);
static void send_game_updates_handler();
int wait_for_updater_thread();
void stop_updates()
{
    set_timer_helper(0, 0); // disarm timer
    has_someone_started_a_game = false;
    return;
}

int check_game_over()
{

    if (num_of_alive_players == 0)
    {
        puts("Game over!\n");
        // int a = *(int *)0; 
        send_game_updates_handler();
        
        stop_updates();
        return UPDATER_THREAD_EXITED;
    }
    else
    {
        return 0;
    }
}

inline static bool isBoundary(uint32_t r, uint32_t c, uint32_t num_of_rows, uint32_t num_of_cols)
{
    return (r == 0 || r == num_of_rows - 1 || c == num_of_cols - 1 || c == 0);
}

static void newMonster(uint32_t i, uint32_t j)
{
    Monster *m = malloc(sizeof(*m));
    if (!m) handle_error("malloc newMonster()");
    Cell *cell = &p_board->cells[i][j];
    cell->isEmpty = false;
    cell->obj.m = m;
    cell->symbol = MONSTER_SYM;
    cell->containsCoin = false;
    m->location.row = i;
    m->location.col = j;
    m->index = num_of_monsters;
    monsters[num_of_monsters++] = m;
}

static int init_board()
{
    // TODO: set seed according to pc time / /dev/urandom
    // FILE *urandom = fopen("/dev/urandom",'r');// doesn't work
    // unsigned int rnd_seed;
    // fread(&rnd_seed, sizeof(unsigned int), 1, urandom);
    // srand(rnd_seed);
    srand(time(0));

    pBoard board = malloc(sizeof(*board)); // TODO: when it might not work?
    p_board = board;
    
    // TODO: not robust enough for a day when board size won't be statically known
    uint32_t num_of_rows = sizeof(board->cells) / sizeof(board->cells[0]);
    uint32_t num_of_cols = sizeof(board->cells[0]) / sizeof(board->cells[0][0]);
    for (uint32_t i = 0; i < num_of_rows; i++)
    {
        for (uint32_t j = 0; j < num_of_cols; j++)
        {
            if (isBoundary(i, j, num_of_rows, num_of_cols))
            {

                board->cells[i][j] = wallCell;
            }
            else if ((rand() % NUM_OF_MONSTERS_DIVIDER) == NUM_OF_MONSTERS_MODULO)
            {
                // printf("putting monster at %d %d\n", i,j);
                newMonster(i, j);
            }
            else if ((rand() % 5) == 2)
            {
                board->cells[i][j] = coinCell;
                num_of_coins_on_screen++;
            }
            else
            {
                board->cells[i][j] = emptyCell;
            }
        }
    }

    board->dimensions.height = num_of_rows;
    board->dimensions.width = num_of_cols;
    return 0;
}

static void create_symbolized_board(char *symbols_board)
{
    // TODO: how to make the type more generic? (taken from typeof(...))
    uint32_t cols = p_board->dimensions.width;
    uint32_t rows = p_board->dimensions.height;
    for (uint32_t i = 0; i < rows; i++)
    {
        for (uint32_t j = 0; j < cols; j++)
        {
            symbols_board[i * cols + j] = p_board->cells[i][j].symbol;
        }
    }
    return;
}

static void set_timer_helper(__suseconds_t tv_tv_usec, __suseconds_t interval_tv_usec)
{
    struct timeval tv = {.tv_usec = tv_tv_usec, .tv_sec = 0};
    struct timeval interval = {.tv_usec = interval_tv_usec, .tv_sec = 0};
    struct itimerval timer_val = {.it_value = tv, .it_interval = interval};

    setitimer(ITIMER_REAL, &timer_val, NULL);
    return;
}

int handle_disconnected_player(int player_fd, int player_idx)
{
    printf("received DISCONNECT event from a client socket with fd=%d\n", player_fd);
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, player_fd, &ev) == -1)
    {
        handle_error("epoll_ctl(EPOLL_CTL_DEL)");
    }

    // Check if num of players == 0  => end game and restart server?
    close(player_fd);
    known_players_fds[player_idx] = -1;
    mark_as_dead(player_idx);
    players_info[player_idx].state = DISCONNECTED;
    return check_game_over();
    // TODO: synchronize
}
void check_for_clients_moves()
{
    for (uint32_t player_idx = 0; player_idx < MAX_NUM_OF_PLAYERS; player_idx++)
    {
        Player *player = &players_info[player_idx];
        Direction *d = &last_move_commands[player_idx];
        Direction cached =  *d;
        if (cached != INVALID_MOVE && player->state == INSIDE_A_GAME)
        {
            // TODO: I don't know how comes *d gets a very large value when server is veryu fast.
            // TODO: doesn't realy help because we need to send the updates to clients in between.
            //     + we need to save the last SUPERPOWER_SPEED moves for each player in order
            //  for this to succeed....
            //  int num_of_rounds = o.p->hasSuperpower ? SUPERPOWER_SPEED : 1;
            //   for (uint32_t i = 0; i < num_of_rounds; i++)
            //  {
            if (cached >= 0 && cached < INVALID_MOVE)
            {

                handle_player_move(player,cached);
            }
            else
            {
                if (INVALID_MOVE != cached && cached != STOPPED)
                    puts("something wrong in check_for_clients_moves, weird direction");
            }
            // *d = INVALID_MOVE;
            // }
        }
    }

    return;
}

static void wonGame()
{
    puts("won game");
    send_game_updates_handler();
    stop_updates();
    pthread_exit(NULL);
    return;
}
static void send_game_updates_handler()
{
    // TODO: should board symbols be sent in small chunks? (~1KB) in order
    //       to not having to store in memory all the board symbols at once?
    char symbols_board[p_board->dimensions.height * p_board->dimensions.width];

    create_symbolized_board(symbols_board); // should be sent before any users movements are received.
    // TODO: use a linked list of registered clients (players)
    int nbytes;
    for (uint32_t i = 0; i < num_of_known_players; i++)
    {
        int fd = known_players_fds[i];
        Player_State state = players_info[i].state;
        bool player_shouldnt_get_update = (state != INSIDE_A_GAME) && (state != DEAD) && (state != SPECTATOR);
        if (-1 == fd || (player_shouldnt_get_update))
        {
            continue;
        }
        nbytes = write(fd, &UPDATE_START, sizeof(UPDATE_START));
        if (nbytes == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                printf("couldn't send UPDATE_START message to fd=%d, will try again on next update\n", fd);
                continue;
            }
            else
            {
                // Example for a scenario when we get here: client disconnected (errno == EPIPE,broken pipe)
                puts("BIG TROUBLES!!!!\n");
                known_players_fds[i] = -1;
                continue;
            }
        }
        fd = known_players_fds[i];
        if (fd == -1)
        {
            continue;
        }
        // TODO: check max size possible to send in one go over sockets
        nbytes = write(fd, symbols_board, p_board->dimensions.height * p_board->dimensions.width);
        if (nbytes < 0)
        {
            // Example for a scenario when we get here: client disconnected (errno == EPIPE,broken pipe)
            puts("WATCH OUT\n");
            continue;
            // TOOD:close connection to client (will get done in the epoll event loop)
        }
        nbytes = write(fd, &UPDATE_END, 1);
        if (nbytes < 0)
        {
            continue;
        }
    }
    return;
}

static int advance_game()
{
    // TODO: create a linked list of monsters pointers...
    for (uint32_t i = 0; i < num_of_monsters; i++)
    {
        if (monsters[i])
        {
          handle_monster_move(monsters[i], INVALID_MOVE);
        }
    }
    return 0;
}

// static void timer_handler(int sig, siginfo_t *info, void *ucontext)
static void timer_handler()
{
    // set_timer_helper(0, 0);
    clock_t start, end; // TODO: move from here?
    start = clock();
    updateSuperPowers();
    check_for_clients_moves();
    advance_game();
    send_game_updates_handler();
    end = clock();
    double cpu_time_diff_usec = (((double)(end - start)) / (CLOCKS_PER_SEC)) * 1000000;
    if (INTERVAL_TV_USEC < cpu_time_diff_usec)
    {
        set_timer_helper(SMALL_TV_USEC, 0);
    }
    else
    {
        set_timer_helper(INTERVAL_TV_USEC - cpu_time_diff_usec, 0);
    }
    return;
}

static void *set_timer_signal(void *blocked_signals_set)
{
    sigset_t *set = (sigset_t *)blocked_signals_set;
    int s, sig;
    send_game_updates_handler(); // first update = initial state
    set_timer_helper(INTERVAL_TV_USEC, 0);
    for (;;)
    {
        //TODO: how come this thread does'nt need to enable the set of signals?
        s = sigwait(set, &sig);
        if (s != 0)
            handle_error_en(s, "sigwait");
        else
        {
            if (sig == SIGUSR1)
            {
                pthread_exit(NULL);
            }
            else
            {
                timer_handler();
            }
        }
    }
    free(set);
    return (void *)NULL;
}

static int start_game()
{
    sigset_t *set = malloc(sizeof(*set));
    sigemptyset(set);
    sigaddset(set, SIGALRM);
    sigaddset(set, SIGUSR1);
    int s = pthread_sigmask(SIG_BLOCK, set, NULL);
    if (s != 0)
        handle_error_en(s, "pthread_sigmask");

    s = pthread_create(&updates_sender_thread, 0, set_timer_signal, (void *)set);
    if (s != 0)
        handle_error_en(s, "pthread_create");
    return 0;
}

int adjustDecrement(uint32_t *x, uint32_t divisor)
{
    bool adjusted = false;
    if (*x == 0)
    {
        *x = divisor - 1;
        adjusted = true;
    }
    return adjusted;
}

int calculateNextLocation(Location *l, Direction dir)
{
    switch (dir)
    {
    case UP:
        if (!adjustDecrement(&l->row, BOARD_HEIGHT))
            l->row = (l->row - 1) % BOARD_HEIGHT;
        break;
    case DOWN:
        l->row = (l->row + 1) % BOARD_HEIGHT;
        break;
    case LEFT:
        if (!adjustDecrement(&l->col, BOARD_WIDTH))
            l->col = (l->col - 1);
        break;
    case RIGHT:
        l->col = (l->col + 1) % BOARD_WIDTH;
        break;
    default:
        exit(EXIT_FAILURE);
    }
    return 0;
}

static Direction random_direction_generator()
{
    static unsigned char rand_bytes[RAND_BYTES_BUF_LEN];
    static uint8_t index = 0;

    if (index == 0)
    {
        uint32_t to_read = sizeof(rand_bytes);
        uint32_t nbytes = getrandom(rand_bytes, to_read, 0);
        if (nbytes != to_read)
        {
            handle_error("getrandom()");
        }
    }
    Direction direction = (Direction)rand_bytes[index] % NUM_OF_DIRECTIONS;
    index = (index + 1) % RAND_BYTES_BUF_LEN;
    return direction;
}

static void mark_as_dead(int player_idx)
{
    num_of_alive_players--;
    players_info[player_idx].state = DEAD;
    last_move_commands[player_idx] = INVALID_MOVE; // just in case...
    superpower_timers[player_idx] = 0;
    return;
}

inline static Cell *getCellAtLocation(Location *l)
{
    return &p_board->cells[l->row][l->col];
}

bool shouldGetSuperPower(unsigned int points)
{
    return (points % SUPERPOWER_DIVIDER == 0);
}

void updateSuperPowers()
{
    for (uint32_t i = 0; i < num_of_known_players; i++)
    {
        if (superpower_timers[i] != 0)
        {
            superpower_timers[i] -= 1;
            if (superpower_timers[i] == 0)
            {
                // TODO : create a function
                Cell *cell = getCellAtLocation(&players_info[i].location);
                cell->symbol = tolower(cell->symbol);
                cell->obj.p->hasSuperpower = false; // TODO: fails here
            }
        }
    }
}

static void destroy_monster(Monster *monster)
{
    uint32_t monster_idx = monster->index;
    free(monster);
    monsters[monster_idx] = NULL;
    // num_of_alive_monsters--;
    return;
}

static void move_player_to_empty_cell(Cell *oldCell, Cell *newCell)
{
    *newCell = *oldCell;
    *oldCell = emptyCell;
    return;
}

// NOTE: this assumes player has already moved!!!
static void check_for_coin_in_target_after_move(Player *player, Cell *oldCell, Cell *newCell)
{
    player->points += COIN_WORTH;
    num_of_coins_on_screen--;
    if (num_of_coins_on_screen == 0)
    {
        wonGame();
    }
    else if (shouldGetSuperPower(player->points))
    {
        (newCell)->symbol = toupper((newCell)->symbol);
        player->hasSuperpower = true;
        superpower_timers[player->index] = NUM_OF_ROUNDS_FOR_SUPERPOWER;
    }
    return;
}

static void handle_player_move(Player *player, Direction d)
{
    Location old_loc, *future_loc;
    Cell *oldCell, *newCell;
    old_loc = player->location;
    future_loc = &player->location;
    oldCell = &p_board->cells[old_loc.row][old_loc.col];
    calculateNextLocation(future_loc, d);

    newCell = &p_board->cells[future_loc->row][future_loc->col];
    if ((newCell)->isEmpty)
    {
        move_player_to_empty_cell(oldCell, newCell);
    }
    else
    {
        bool coin_in_destination = (newCell)->containsCoin;

        if ((newCell)->symbol == MONSTER_SYM)
        {
            if (player->hasSuperpower)
            {
                printf("player moved and killed Monster %u \n",(newCell)->obj.m->index);
                destroy_monster((newCell)->obj.m);
                move_player_to_empty_cell(oldCell, newCell);
                if (coin_in_destination)
                    check_for_coin_in_target_after_move(player, oldCell, newCell);
                // return MONSTER_DIED;
            }
            else
            {
                mark_as_dead(player->index);
                *oldCell = emptyCell;
                if (UPDATER_THREAD_EXITED == check_game_over())
                    pthread_exit(NULL);
            }
        }
        else if (coin_in_destination)
        {
            move_player_to_empty_cell(oldCell, newCell);
            check_for_coin_in_target_after_move(player, oldCell, newCell);
        }
        else
        {
            // if not a coin cell, then it is either a wall or another player
            player->location = old_loc;
        }
    }
    return;
}

static inline void update_source_cell(Cell *oldCell)
{
    *oldCell = (oldCell)->containsCoin ? coinCell : emptyCell;
}

static int handle_monster_move(Monster *monster, Direction d)
{
    static unsigned int num_of_tries = 0;
    Location old_loc ;
    Cell *oldCell, *newCell;
    Direction rand_direction;
    char newCellSym;
    old_loc = monster->location; //backup

    oldCell = &p_board->cells[old_loc.row][old_loc.col];
    rand_direction = random_direction_generator();
    calculateNextLocation(&monster->location, rand_direction);
    newCell = &p_board->cells[monster->location.row][monster->location.col];
    newCellSym = (newCell)->symbol;

    num_of_tries = 0;

    while ((newCellSym == WALL_SYM) || (newCellSym == MONSTER_SYM))
    {
        monster->location = old_loc;
        if (++num_of_tries == MONSTER_TRIES_TRESHOLD)
        {
            return -1;
        }

        // TODO: code duplication (2 lines)
        rand_direction = random_direction_generator();
        calculateNextLocation(&monster->location, rand_direction);
        newCell = &p_board->cells[monster->location.row][monster->location.col];
        newCellSym = (newCell)->symbol;
    }

    // from this point, we are assured newCell is not a monster or wall
    if ((newCell)->isEmpty || newCellSym == COIN_SYM)
    {
        bool dest_has_coin = (newCellSym == COIN_SYM);
        *newCell = *oldCell; 
        (newCell)->containsCoin = dest_has_coin; // set or reset this flag.
        update_source_cell(oldCell);
    }
    else if ((newCell)->obj.p->hasSuperpower)
    // monster steps in and encounters a player (happens only due to random moves)
    {
        // Monster gets killed by player
        printf(" Monster %u steps and gets killed by player\n",monster->index);
        destroy_monster(monster);
        // TODO: check if all monsters are dead....?
        update_source_cell(oldCell);
        return MONSTER_DIED;
    }
    else
    {
        // monster kills player
        printf(" Monster %u steps and gets killed by player\n",monster->index);
        mark_as_dead((newCell)->obj.p->index);
        *newCell = *oldCell;
        update_source_cell(oldCell);
        if (UPDATER_THREAD_EXITED == check_game_over())
            pthread_exit(NULL);
    }
    return 0;
}

//TODO: implement!!!
static char *getLocalNetIP(char *ip_buf)
{
    if (!ip_buf)
    {
        handle_error("getLocalNetIP()");
    }

    return strcpy(ip_buf, "127.0.0.1"); // Dummy ip
}

static int displayIP()
{
    char ip_buf[IP_MAX_CHAR_LEN];
    char *ip = getLocalNetIP(ip_buf); //TODO: fix
    if (!ip)
    {
        handle_error("displayIP()");
    }

    // intToIPV4(ip, ip_buf);
    printf("Server ip: %s\n", ip_buf);
    return 0;
}

static int setnonblocking(int socket_fd)
{
    int file_flags = fcntl(socket_fd, F_GETFL);

    // THIS DOESN"T MODIFY THE FLAGS for some reason...
    // int optval = 1;
    // setsockopt(socket_fd, SOL_SOCKET, SOCK_NONBLOCK, &optval, sizeof(optval));
    fcntl(socket_fd, F_SETFL, file_flags | O_NONBLOCK);
    file_flags = fcntl(socket_fd, F_GETFL);
    return 0;
}

static int handle_player_command(int player_fd, int playerIndex, char *command, int n)
{
    // printf("handling player message of len %d  first byte %hhX\n, n,command[0]");
    if (n > MOVE_COMMNAD_LEN)
    {
        // user sent his/her name
        command[n] = '\0';
        strncpy(players_info[playerIndex].name, command, MAX_COMMAND_LEN);
    }
    else
    {
        Direction d = decodeDirection((unsigned char*)command);
        // printf("got a move! %d\n",d);
        last_move_commands[playerIndex] = d;
    }
    return 0;
}

static int getPlayerIndex(int player_fd)
{
    for (int i = 0; i < num_of_known_players; i++)
    {
        if (known_players_fds[i] == player_fd)
        {
            return i;
        }
    }
    return IT_IS_A_NEW_PLAYER;
}

// For debugging porpouses
// static void decipherEventFlags(struct epoll_event ev)
// {
//     // TODO: it will only show flags that were configured on initialization
//     char buf[100] = {0};
//     sprintf(buf, "events flags: 0x%x -", ev.events);
//     strcat(buf, " <");
//     if (ev.events & EPOLLIN)
//         strcat(buf, "ready_READ |");
//     if (ev.events & EPOLLOUT)
//         strcat(buf, "READY_WRITE |"); // a message was written through the client socket (by the player)

//     // added automatically
//     if (ev.events & EPOLLHUP)
//         strcat(buf, "hung up |");

//     if (ev.events & EPOLLPRI)
//         strcat(buf, "PRI |");
//     if (ev.events & EPOLLWAKEUP)
//         strcat(buf, "wakeup |");
//     strcat(buf, "> ");
//     printf("%s ", buf);
//     return;
// }

// TODO: check if name was already supplid through CLI!
// returns the index where the player was added (will be a linked list later)
static int addToKnownPlayers(int player_fd)
{
    int retval = num_of_known_players;
    known_players_fds[num_of_known_players++] = player_fd;
    num_of_alive_players++;
    return retval;
}

static int put_player_on_board(Player *p)
{
    long long unsigned int num_of_tries = 0;
    uint32_t row_idx, col_idx;
    bool found_empty_place = false;
    while (num_of_tries < BOARD_HEIGHT * BOARD_WIDTH)
    {
        row_idx = rand() % BOARD_HEIGHT;
        col_idx = rand() % BOARD_WIDTH;
        if (!p_board->cells[row_idx][col_idx].isEmpty)
        {
            num_of_tries++;
        }
        else
        {
            found_empty_place = true;
            p_board->cells[row_idx][col_idx].obj.p = p;
            p_board->cells[row_idx][col_idx].isEmpty = false;
            p->location.row = row_idx;
            p->location.col = col_idx;
            p_board->cells[row_idx][col_idx].symbol = current_player_symbol++;
            break;
        }
    }
    return found_empty_place ? 0 : EXIT_FAILURE;
}

int notify_player_on_game(int fd)
{
    uint32_t val;

    int nbytes = write(fd, &GAME_STARTED, sizeof(GAME_STARTED)); // TODO: CHANGE TO A CONSTANT (DEFINE)
    if (nbytes == -1)
    {
        handle_error("write() game_started");
    }

    val = p_board->dimensions.height;
    nbytes = write(fd, &val, member_size(Dimensions, height));
    if (nbytes == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            printf("couldnt write height to fd = %d\n", fd);
            // TODO: what to do here? kill client?
            return -1;
        }
        else
            handle_error("write() height");
    }
    printf("nbytes=%d\n",nbytes);
    puts("wrote height");
    nbytes = write(fd, &(p_board->dimensions.width), member_size(Dimensions, width));
    printf("nbytes=%d\n",nbytes);
    puts("wrote width");
    return 0;
}

int new_game()
{
    init_board();
    has_someone_started_a_game = true;
    return 0;
}

int join_existing(Player *player)
{
    // puts("joining game\n");
    return 0;
}

int wait_for_others(Player *player)
{
    puts("waiting\n");
    player->state = WAIT_FOR_OTHERS;
    return 0;
}

int readChoice(int player_fd, int playerIndex, Player *player)
{
    char choice;

    ssize_t nbytes_read = read(player_fd, &choice, MAX_OPTION_LEN);
    if (nbytes_read < 0)
    {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
        {
            // puts("will try again later to get user choice\n");
            player->state = WAITING_FOR_CHOICE;
            return TRY_AGAIN_LATER;
        }
    }
    else if (nbytes_read == 0)
    {
        puts("client disconnected... remove it from the epoll please\n");
    }
    else if (MAX_OPTION_LEN == nbytes_read)
    {
        return (atoi(&choice));
    }
    else
    {
        printf("read partial info of %ld\n", nbytes_read);
        handle_error("readChoice::read()");
        return 0;
    }
    return -1;
}

int showOptions(int player_fd, int playerIndex)
{
    int nbytes;
    char small_message_buf[SMALL_MSG_MAX_LEN];
    strcpy(small_message_buf, "Enter your choice:\n");
    strcat(small_message_buf, "1. Start new game\n");
    strcat(small_message_buf, "2. Join existing game\n");
    strcat(small_message_buf, "3. Wait for other players\n");
    // TODO: should I replace all writes (non-blocking) with a wrapper that deals with error?
    //  nbytes = write(player_fd, &ENTER_YOUR_CHOICE_MSG, sizeof(ENTER_YOUR_CHOICE_MSG));

    nbytes = write(player_fd, small_message_buf, strlen(small_message_buf) + 1);
    if (nbytes <= 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            puts("not ready for write\n");
        }
        else
        {
            handle_error("write()");
        }
    }

    return 0;
}

void inquiryForPlayerName(int player_fd, int playerIndex)
{
    write(player_fd, &I_NEED_YOUR_NAME_MSG, sizeof(I_NEED_YOUR_NAME_MSG)); // or use send()
    players_info[playerIndex].state = WAS_ASKED_FOR_NAME;
    return;
}

int handle_known_player(int player_fd, int player_idx, Player *player)
{
    int n;
    char small_message_buf[SMALL_MSG_MAX_LEN];
    // TODO: might not work for movements commands from client (1 byte)
    n = read(player_fd, small_message_buf, SMALL_MSG_MAX_LEN);
    if (n == 0)
    {
        if (handle_disconnected_player(player_fd, player_idx) == UPDATER_THREAD_EXITED)
        {
            wait_for_updater_thread();
        }
    }
    else if (n > 0)
    {
        // TODO: should depend on state...
        handle_player_command(player_fd, player_idx, small_message_buf, n);
    }
    else
    {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
        {
            return TRY_AGAIN_LATER;
        }
        else
        {
            printf("errno= %d\n",errno);
            //Connection reset by peer
            if (handle_disconnected_player(player_fd,player_idx) == UPDATER_THREAD_EXITED){
                    pthread_exit(NULL);
            }
            // handle_error("read()");
        }
    }
    return 0;
}

int handle_user_choice(int choice, Player *player, int player_fd)
{
    switch (choice)
    {
    case START_NEW_GAME:
        if (!has_someone_started_a_game)
        {
            player->state = WAITING_FOR_CLIENT_TO_ACK_GAME;
            game_initiator_idx = player->index;
            new_game();
            notify_player_on_game(player_fd);
        }
        break;
    case JOIN_EXISTING_GAME:
        player->state = WAITING_FOR_CLIENT_TO_ACK_GAME;
        join_existing(player);
        notify_player_on_game(player_fd); // TODO: this assumes joining a game can't happen before start game request..., FIX!
        break;

    case WAIT_FOR_OTHERS:
        player->state = WAITING_FOR_GAME_TO_START;
        wait_for_others(player);
        break;
    default:
        puts("invalid choice\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int getUserAck(int player_fd)
{
    char buf[CONTROL_CMD_SIZE];
    int nbytes = read(player_fd, buf, CONTROL_CMD_SIZE);
    if (nbytes == -1)
    {
        // printf("didn't get getUserAck() for fd %d\n", player_fd);
        return -1; // try again later...
    }

    if (buf[0] == CLIENT_ACK_GAME_STARTED)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

int read_name(int player_fd, int playerIndex)
{
    char small_message_buf[MAX_NAME_LENGTH];
    // TODO: check if name was already send on socket (from client argv[3])
    int n = read(player_fd, small_message_buf, MAX_NAME_LENGTH); // should be NONBLOCKING! (otherwise we must use a thread for each player)
    if (n <= 0)
    {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
        {
            players_info[playerIndex].state = WAITING_FOR_NAME;
            return TRY_AGAIN_LATER;
        }
        else
        {
            handle_error("read_name::read()");
            // TODO: remove client fd from epoll!
        }
    }
    else
    {
        small_message_buf[n] = '\0';
        strcpy(players_info[playerIndex].name, small_message_buf);
        players_info[playerIndex].state = RECEIVED_NAME;
    }
    return 0;
}

int handle_player_event(int n)
{
    // TODO: make sure it's ok to pass here a stack pointer (small_message_buf)
    int player_fd = events[n].data.fd;
    int player_idx;
    Player *player_status = NULL;
    player_idx = getPlayerIndex(player_fd);
    player_status = &players_info[player_idx];
    // TODO: replace with switch case?

    if (IT_IS_A_NEW_PLAYER == player_idx)
    {
        if (num_of_known_players < MAX_NUM_OF_PLAYERS)
        {
            player_idx = addToKnownPlayers(player_fd);
            player_status->state = JUST_LOGGED_IN;
            player_status->index = player_idx;
            player_status->hasSuperpower = false;
        }

        // else
        // {
        //     strcpy(small_message_buf, "Sorry, Can't add more players at the moment");
        //     write(player_fd, small_message_buf, strlen(small_message_buf) + 1); // or use send()
        // }
    }

    if (JUST_LOGGED_IN == player_status->state)
    {
        inquiryForPlayerName(player_fd, player_idx);
    }

    if (player_status->state == WAS_ASKED_FOR_NAME || player_status->state == WAITING_FOR_NAME)
    {
        // puts("calling read_name()\n");
        int status = read_name(player_fd, player_idx);
        if (TRY_AGAIN_LATER == status)
        {
            puts("will try again later to get name\n");
            return TRY_AGAIN_LATER;
        }
    }

    if (player_status->state == RECEIVED_NAME)
    {
        int nbytes = write(player_fd, &ENTER_YOUR_CHOICE_MSG, sizeof(ENTER_YOUR_CHOICE_MSG));
        if (nbytes == -1)
        {
            handle_error("Enter your choice write()");
        }
        player_status->state = WAITING_FOR_OPTIONS;
    }

    if (player_status->state == WAITING_FOR_OPTIONS)
    {
        showOptions(player_fd, player_idx);
        player_status->state = WAS_SHOWN_OPTIONS;
    }
    // TODO: update robustness
    if (player_status->state == WAS_SHOWN_OPTIONS || player_status->state == WAITING_FOR_CHOICE)
    {
        int choice = readChoice(player_fd, player_idx, player_status);
        if (TRY_AGAIN_LATER == choice)
        {
            // puts("will try again later to get choice\n");
            return 2;
        }
        else if (choice == -1)
        {
            if (UPDATER_THREAD_EXITED == handle_disconnected_player(player_fd, player_idx))
                return UPDATER_THREAD_EXITED;
        }
        // TODO: validation should be on server side!
        else if ((choice <= MAX_OPTION) && (choice >= MIN_OPTION))
        {
            handle_user_choice(choice, player_status, player_fd);
        }
        else
        {
            printf("%d \n", choice);
            puts("Now kicking out the client for invalid choice\n");
            close(player_fd); // TODO:
        }
    }
    else if (player_status->state == WAITING_FOR_CLIENT_TO_ACK_GAME)
    {
        // if (someone_started_a_game)
        int retval = getUserAck(player_fd);

        if (0 == retval)
        {
            // TODO: need to handle state when all players left the game... (game_initiator_idx)
            if (game_initiator_idx == player_idx)
            {
                start_game();
            }
            if (put_player_on_board(player_status) == 0)
            {
                player_status->state = INSIDE_A_GAME;
            }
            else
            {
                exit(EXIT_FAILURE);
            }
        }
        else if (-1 == retval)
        {
            puts("didn't recieve ack.\n");
            return -1;
        }
    }
    else if (player_status->state == INSIDE_A_GAME)
    {
        // TODO: will get updated soon (when game ends...)
        //  puts("now handling player request\n");
        // TODO: check retval!
       handle_known_player(player_fd, player_idx, player_status);
    }

    return 0;
}

int wait_for_updater_thread()
{
    int status = pthread_join(updates_sender_thread, NULL);
    if (status != 0)
    {
        printf("%d", status);
        handle_error("pthread_join()");
    }
    return 0;
}


/*
While the usage of epoll when employed as a level-triggered interface does have the same semantics as poll(2),
 the  edge-triggered  usage requires more clarification to avoid stalls in the application event loop.
   In this example, listener is        a nonblocking socket on which listen(2) has been called.
     The function do_use_fd() uses the new ready file descriptor un‐
       til EAGAIN is returned by either read(2) or write(2).
        An event-driven state machine application should, after having re‐
       ceived EAGAIN, record its current state so that at the next call to do_use_fd()
       it will continue to read(2)  or  write(2)
       from where it stopped before.
*/
int epoll_setup()
{
    int client_sockfd;
    struct sockaddr_in client_sockaddr;
    socklen_t addrlen = sizeof(client_sockaddr);

    // flag can be zero or FD_CLOEXEC
    epollfd = epoll_create1(0);
    if (epollfd == -1)
    {
        handle_error("epoll_create1");
    }

    ev.events = EPOLLIN;      // notify when The associated file is available for read(2) operations.
    ev.data.fd = listen_sock; // Data will come from the listening socket

    // Interest in particular file descriptors is then registered via epoll_ctl(2),
    // which adds items to the interest list of the epoll instance.
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1)
    {
        handle_error("epoll_ctl: listen_sock");
    }

    for (;;)
    {
        // waits for I/O events, blocking the calling thread if no events are currently available.
        // (i.e fetching items from the ready list of the epoll instance.)
        // nfds stores the number of ready file descriptors

        // TODO: this seems to enter the loop even after client disconnects!!! because there will be nothing to wait on
        //     Even though it should be a blocking call
        while (((nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1)) == -1) && (errno == EINTR))
        {
            puts("someone (signal) interuppted the epoll_wait()\n");
            // if (num_of_known_players == 0)
        }
        if (nfds == -1)
        {
            printf("errno = %d ", errno);
            handle_error("epoll_wait");
        }

        // accepting new clients can be done in another thread! (which adds them to the event poll)
        for (int n = 0; n < nfds; ++n)
        {
            // decipherEventFlags(events[n]);
            //TOOD: might need at some point to check the event other fields in order to distinct between different
            //      type of events

            // If we are currently handling the server fd's
            // accept new connection and add it to the epoll events
            if (events[n].data.fd == listen_sock)
            {
                // puts("received connection request! \n");
                client_sockfd = accept(listen_sock,
                                       (struct sockaddr *)&client_sockaddr, &addrlen);
                if (client_sockfd == -1)
                {
                    handle_error("accept");
                }

                // we don't want to wait for players to make a move
                if (0 != setnonblocking(client_sockfd))
                {
                    handle_error("setnonblocking()");
                }

                // An application that employs the EPOLLET flag should use nonblocking file descriptors to
                // avoid having a blocking read or write starve a task that is handling multiple file descriptors.
                //   The suggested way to use  epoll  as  an
                // edge-triggered (EPOLLET) interface is as follows:
                // i   with nonblocking file descriptors; and
                // ii  by waiting for an event only after read(2) or write(2) return EAGAIN.

                // When  used  as an edge-triggered interface, for performance reasons,
                // it is possible to add the file descriptor inside the
                // epoll interface (EPOLL_CTL_ADD) once by specifying (EPOLLIN|EPOLLOUT).
                //  This allows you to avoid  continuously  switching
                // between EPOLLIN and EPOLLOUT calling epoll_ctl(2) with EPOLL_CTL_MOD.
                ev.events = EPOLLIN | EPOLLOUT | EPOLLET; // TODO: READ+WRITE ? , EPOLLET = Edge trigger
                // ev.events = EPOLLIN | EPOLLOUT | EPOLLET; // TODO: READ+WRITE ? , EPOLLET = Edge trigger
                ev.data.fd = client_sockfd;

                // int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
                // This  system  call is used to add, modify, or remove entries in the interest list of
                // the epoll(7) instance referred to by the file descriptor epfd.
                //  It requests that the operation op be performed for the target file descriptor, fd.
                // EPOLL_CTL_ADD-  Add fd to the interest list and associate the settings specified in
                // event with the internal file linked to fd.
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sockfd, &ev) == -1)
                {
                    handle_error("epoll_ctl: conn_sock");
                }
            }
            else
            {
                if (handle_player_event(n) == UPDATER_THREAD_EXITED)
                {
                    wait_for_updater_thread();
                }
                // TODO: check retval
            }
        }
    }
}

int setup()
{
    memset(last_move_commands, INVALID_MOVE, sizeof(MAX_NUM_OF_PLAYERS)); // TODO: implementation dependant!! not always 1 byte size

    struct sockaddr_in my_addr;

    displayIP(); // TODO: fix!!
    listen_sock = createListenerSocket();
    if (SOCKET_ERROR == listen_sock)
    {
        handle_error("socket()");
    }
    printf("server socket fd=%d\n", listen_sock);
    setSocketOption(listen_sock, SO_REUSEADDR, 1);
    setSocketOption(listen_sock, SO_REUSEPORT, 1);

    memset(&my_addr, 0, sizeof(struct sockaddr_in));
    bindServerSocket(listen_sock, LISTEN_PORT);
    transformIntoAListener(listen_sock);
    epoll_setup();
    return 0;
}

int main()
{
    setup();
    return 0;
}
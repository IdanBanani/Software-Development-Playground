#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/random.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/param.h>
#include <signal.h>
#include <semaphore.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netdb.h>
#include <error.h>
#include <errno.h>
#include <arpa/inet.h>
#include "termios2.h"
#include "colors.h"
#include "../common/protocol.h"
// #include "../common/common.h"


#define BUF_SIZE 4096
#define RAND_BYTES_BUF_LEN 256

#define UPDATES_TIMEOUT_SEC 10 
#define SLEEP_USEC 10
#define RENDER_SCREEN_USEC (50000)

// FOR TIMER of bot
#define TV_TV_USEC (500000)
#define INTERVAL_TV_USEC (50000)

static int _server_fd = -1;

static pthread_cond_t have_new_keystroke = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t updates_lock = PTHREAD_MUTEX_INITIALIZER;
static bool isGameOver = false;
static char currentMove = -1;
static char last_sent_move = -1;

void startGameinClientSide(bool isClientHuman, Dimensions dims);
unsigned char encodeDirection(Direction d);

static void timer_handler(int sig, siginfo_t *info, void *ucontext)
{
    static unsigned char rand_bytes[RAND_BYTES_BUF_LEN];
    static uint32_t to_read = sizeof(rand_bytes);
    static uint8_t index = 0;
    if (index == 0)
    {
        uint32_t nbytes = getrandom(rand_bytes, to_read, 0);
        if (nbytes != to_read)
        {
            handle_error("getrandom()");
        }
    }
    char cmd = rand_bytes[index] % NUM_OF_DIRECTIONS;
    index = (index + 1) % RAND_BYTES_BUF_LEN;
    pthread_mutex_lock(&updates_lock);
    if (last_sent_move != cmd)
    {
        currentMove = cmd;
        pthread_cond_signal(&have_new_keystroke);
    }
    pthread_mutex_unlock(&updates_lock);
    return;
}

static void set_timer_helper(__suseconds_t tv_tv_usec, __suseconds_t interval_tv_usec)
{
    struct itimerval timer_val;
    struct timeval tv;
    struct timeval interval;
    tv.tv_usec = tv_tv_usec;
    tv.tv_sec = 0;
    interval.tv_usec = interval_tv_usec;
    interval.tv_sec = 0;

    timer_val.it_value = tv;
    timer_val.it_interval = interval;

    setitimer(ITIMER_REAL, &timer_val, NULL);
    return;
}

static void set_timer_signal()
{
    // TODO: should they be allocated on heap?
    //  struct sigaction new_action = {0};
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    struct sigaction old_action;
    new_action.sa_flags |= SA_SIGINFO; // sa_sigaction (instead of sa_handler) specifies the signal-handling function for signum.
    new_action.sa_flags |= SA_RESTART;
    new_action.sa_sigaction = timer_handler;

    // Do not prevent the signal from being received from within its own signal handler.
    //  This flag is only meaningful when establishing a signal handler.
    // Do not add the signal to the thread's signal mask while
    //   the handler is executing, unless the signal is specified in act.sa_mask.
    // Consequently, a further instance of the
    // signal may be delivered to the thread while it is executing the handler.

    // new_action.sa_flags |= SA_NODEFER; // (nested signal) the signal which triggered the handler will be blocked, unless the SA_NODEFER flag is used

    sigaction(SIGALRM, &new_action, &old_action);
    set_timer_helper(TV_TV_USEC, INTERVAL_TV_USEC);
    return;
}

int connectToServer(char *server_ip, char *port)
{
    int sfd, s;
    struct addrinfo hints = {0};
    struct addrinfo *result, *rp;

    /* Obtain address(es) matching host/port. */
    memset(&hints, 0, sizeof(hints));
    // hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_family = AF_INET;       /* IPv4 */
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = 0;
    hints.ai_protocol = 0; /* Any protocol */
    // hints.ai_addr = server_ip // we can only set the interface!
    hints.ai_addr = INADDR_ANY;

    s = getaddrinfo(server_ip, port, &hints, &result);
    if (s != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

    /* getaddrinfo() returns a list of address structures.
              Try each address until we successfully connect(2).
              If socket(2) (or connect(2)) fails, we (close the socket
              and) try the next address. */
    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break; /* Success */

        close(sfd);
    }

    freeaddrinfo(result); /* No longer needed */

    if (rp == NULL)
    { /* No address succeeded */
        fprintf(stderr, "Could not connect (ON all interfaces?)\n");
        exit(EXIT_FAILURE);
    }
    _server_fd = sfd; // TODO: is it ok to put it here?
    return sfd;
}

void printBoard(Dimensions dim, char *buf)
{
    //  TODO: find a better way to do the prints if neccessary
    char c;
    for (uint32_t i = 0; i < dim.height; i++)
    {
        for (uint32_t j = 0; j < dim.width; j++)
        {
            c = buf[i * dim.width + j];
            switch (c)
            {
            case MONSTER_SYM:
                printf(RED);
                break;
            case COIN_SYM:
                printf(GREEN);
                break;
            case WALL_SYM:
                printf(BLUE);
                break;
            case EMPTY_SYM:
                printf(WHITE);
                break;
            default:
                printf(CYAN);
                break;
            }
            printf("%c" RESET, c);
        }
        printf("\r\n");
    }
    fflush(stdout);
}
// TODO: don't forget to cancel the thread when game is over
void *receiveUpdates(void *arg)
{
    Dimensions dim = *((Dimensions *)arg);
    char buf[BUF_SIZE] = {0};
    struct pollfd fd;
    int ret;
    fd.fd = _server_fd; // your socket handler
    fd.events = POLLIN;

    while (!isGameOver)
    {
        ret = poll(&fd, 1, 3000); // 3 second for timeout
        switch (ret)
        {
        case -1:
            handle_error("poll");
            break;
        case 0:
            // Timeout
            puts("game over!\r\n");
            fflush(stdout);
            isGameOver = true;
            break;
        default:
            while (buf[0] != UPDATE_START && buf[0] != GAME_OVER)
            {

                // TODO: after game is over, thread gets stuck in here!
                read(_server_fd, buf, sizeof(UPDATE_START));
            }

            if (buf[0] == GAME_OVER)
            {
                puts("game over!\r\n");
                fflush(stdout);
                isGameOver = true;
                break;
            }

            // information was already read
            usleep(RENDER_SCREEN_USEC);
            CLEAR_SCREEN();
            read(_server_fd, buf, dim.height * dim.width);
            // char *board_symbolic =(char*) malloc(1+ (dim.width+1) *(dim.height));
            // for (uint32_t i = 0; i < dim.height; i++)
            // {
            //     memcpy(board_symbolic+i*(dim.width+1),buf+i*(dim.width),dim.width);
            //     board_symbolic[(i)*(dim.width)] = '\n';
            // }
            // board_symbolic[(dim.width+1) *(dim.height)] = '\0';
            // printf("%s\n",board_symbolic);
            // free(board_symbolic);

            printBoard(dim, buf);

            read(_server_fd, buf, sizeof(UPDATE_END));
            if (buf[0] != UPDATE_END)
            {
                puts("Something wrong... exiting\r\n");
                exit(EXIT_FAILURE);
            }

            break;
        }
    }

    puts("Game over, stopped receiving updates. press any key to exit.\r\n");
    fflush(stdout);
    pthread_exit(0);
}

int send_move_commands()
{
    int nbytes, retval;
    while (!isGameOver)
    {
        pthread_mutex_lock(&updates_lock);
        while ((currentMove == -1))
        {
            // TODO: when game ends, the thread is stuck here.
            retval = pthread_cond_wait(&have_new_keystroke, &updates_lock);
            if (retval == -1)
            {
                handle_error("pthread_cond_wait()");
            }
        }
        last_sent_move = currentMove;
        currentMove = -1;
        char cmd = encodeDirection(last_sent_move % NUM_OF_DIRECTIONS); // TODO: redundant?
        pthread_mutex_unlock(&updates_lock);
        nbytes = write(_server_fd, &cmd, MOVE_CMD_SIZE); // TODO: possible race condition?
        if (nbytes == -1)
        {
            handle_error("send_move_commands()::write()");
        }
        usleep(SLEEP_USEC);
    }
    return 0;
}

Dimensions getDimensions(int _server_fd)
{
    int nbytes;
    Dimensions dimensions; // TODO: is it ok to use stack addr?
    nbytes = read(_server_fd, &dimensions.height, sizeof(dimensions.height));
    if (nbytes == -1)
        handle_error("getDimensions()::read() height");
    nbytes = read(_server_fd, &dimensions.width, sizeof(dimensions.width));
    if (nbytes == -1)
        handle_error("getDimensions()::read() width");
    printf("%u x %u\n", dimensions.height, dimensions.width);
    return dimensions;
}

void handleDisconnection()
{
    shutdown(_server_fd, SHUT_RDWR);
    close(_server_fd);
    puts("Server disconnected :(\n");
    exit(EXIT_FAILURE); // TODO: restart client instead of kiling the process!
}

void startGameinClientSide(bool isClientHuman, Dimensions dims)
{
    pthread_t updates_listener, keyboard_listener;
    pthread_condition condition = {.cond = &have_new_keystroke,
                                   .lock = &updates_lock};
    synchronize_info info = {.pc = condition, .movement = &currentMove, .isGameOver = &isGameOver};

    // didnt work
    //  struct timeval tv;
    //  tv.tv_sec = UPDATES_TIMEOUT_SEC;
    //  tv.tv_usec = 0;
    //  setsockopt(_server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // TODO: is using stack addr dangerous?
    pthread_create(&updates_listener, NULL, receiveUpdates, &dims); //

    // TODO: note- stack addr
    if (isClientHuman)
        pthread_create(&keyboard_listener, NULL, start_keyboard_listener, &info);
    else
        set_timer_signal();

    send_move_commands(); // should not return until game ends
    if (isClientHuman)
    {
        puts("killing keyboard_listener");
        fflush(stdout);
        reset_terminal_mode();
        // TODO: or use pthread_cancel?
        pthread_kill(keyboard_listener, SIGTERM);
        pthread_kill(keyboard_listener, SIGINT);
        pthread_kill(keyboard_listener, SIGQUIT);
        pthread_kill(keyboard_listener, SIGUSR1);
        pthread_cancel(keyboard_listener);
        pthread_join(keyboard_listener, NULL);
    }
    else
    {
        set_timer_helper(0, 0);
    }
    pthread_join(updates_listener, NULL);
    return;
}

int interactWithServer(bool isClientHuman, char *name)
{
    char buf[BUF_SIZE];
    bool isClientInAGame = false;
    ssize_t nread = -1;
    Dimensions dimensions;

    while (!isClientInAGame)
    {
        nread = read(_server_fd, buf, CONTROL_CMD_SIZE);
        if (nread == 0)
        {
            handleDisconnection();
        }
        else if (nread < 0)
        {
            handle_error("read()");
        }
        else if (nread == CONTROL_CMD_SIZE)
        {
            // TODO: should use switch/case instead?
            //  TODO: should client answer with a special code before sending the name?
            if (buf[0] == I_NEED_YOUR_NAME_MSG)
            {
                if (name)
                {
                    write(_server_fd, name, MIN((strlen(buf) + 1), MAX_NAME_LENGTH));
                }
                else
                {
                    puts("Please enter your name:\n");
                    fgets(buf, MAX_NAME_LENGTH, stdin);
                    buf[MAX_NAME_LENGTH - 1] = '\0';
                    write(_server_fd, buf, strlen(buf) + 1);
                }
            }
            else if (buf[0] == ENTER_YOUR_CHOICE_MSG)
            {
                nread = read(_server_fd, buf, BUF_SIZE); // TODO: check retval
                fputs(buf, stdout);
                int choice = getchar();
                buf[0] = choice;
                write(_server_fd, buf, 1); // TODO: what length variable should be used here?
            }
            else if (buf[0] == GAME_STARTED)
            {
                printf("\ngetting game info...\n");
                dimensions = getDimensions(_server_fd);
                write(_server_fd, &CLIENT_ACK_GAME_STARTED, CONTROL_CMD_SIZE);
                isClientInAGame = true;
            }
        }
    }

    startGameinClientSide(isClientHuman, dimensions);
    return 0;
}

int main(int argc, char *argv[])
{
    // setvbuf(stdout, NULL, _IONBF, 0);

    // TODO: can the client figure out the server ip in LAN
    //   by itself using only the port num?

    if (argc < 3)
    {
        // TODO : use option flags!
        fprintf(stderr, "Usage: %s host_IP port [a/h] [name]...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *server_ip = strdup(argv[1]);

    if (!strchr(server_ip, '.'))
    {
        free(server_ip);
        server_ip = "127.0.0.1";
    }

    printf("ip:%s\n", server_ip);

    char *port = argv[2];
    bool isClientHuman = (argc >=4 && argv[3][0] == 'h');
    char *name = NULL;
    if (argc >= 5)
    {
        name = strdup(argv[4]); // TODO: necessary?
    }

    connectToServer(server_ip, port);
    interactWithServer(isClientHuman, name);
    return 0;
}

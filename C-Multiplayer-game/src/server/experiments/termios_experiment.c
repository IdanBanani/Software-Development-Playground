#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/select.h>

#define CLEAR_SCREEN() printf("\e[1;1H\e[2J")
// '\x1b[2J\x1b[H',
// printf("\033c");

static fd_set fdset;
static struct termios orig_termios, newt;
bool exit_flag_thread_listening, exit_flag_connected;

// tcflag_t c_iflag;      /* input modes */
// tcflag_t c_oflag;      /* output modes */
// tcflag_t c_cflag;      /* control modes */
// tcflag_t c_lflag;      /* local modes */
// cc_t     c_cc[NCCS];   /* special characters */

// An individual terminal special character can be disabled
// by setting the value of the corresponding c_cc element to _POSIX_VDISABLE.

// TCSANOW
// the change occurs immediately.
// TCSADRAIN
// the change occurs after all output written to fd has been transmitted. This option should be used when changing parameters that affect output.
// TCSAFLUSH
// the change occurs after all output written to the object referred by fd has been transmitted, and all input
// that has been received but not read will be discarded before the change is made.

// static struct termios old, current;

/* Set *T to indicate raw mode.  */
/*
input is available character by character, echoing is disabled,
and all special processing of terminal input and output characters is disabled.
*/
void cfmakeraw(struct termios *t)
{
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1; /* read returns when one char is available.  */
    t->c_cc[VTIME] = 0;
}

int getch(unsigned char *ch)
{
    return read(0, ch, sizeof(*ch));
}

void reset_terminal_mode()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios); // restore
    setlinebuf(stdout);                              // needed?
    printf("\033c");                                 // clear screen
}

int setup()
{

    tcgetattr(STDIN_FILENO, &orig_termios); /* grab old terminal i/o settings */
                                            //   fcntl(fd, F_GETFL)
    newt = orig_termios;                    /* make new settings same as old settings */

    /* register cleanup handler, and set the new terminal mode */
    atexit(reset_terminal_mode);

    cfmakeraw(&newt);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt); /* use these new terminal i/o settings now */

    printf("\033c"); // Clear window
    FD_ZERO(&fdset);
    FD_SET(STDIN_FILENO, &fdset);

    // setbuf(stdout, NULL);
    setbuf(stdin, NULL);
    printf("\e[8;30;80t"); // Resize window

    return 0;
}

void handleInput(unsigned char *ch)
{
    // @doc ESCAPE SEQUENCES arrow codes - http://stackoverflow.com/questions/10463201/getch-and-arrow-codes
    if (*ch == '\33')
    {
        getch(ch); // skip the [
        switch (getch(ch))
        { // the real value
        case 'A':

            // printf("ESC[1A"); // code for arrow up
            puts("up");
            break;
        case 'B':
            // printf("ESC[1B"); // code for arrow down
            puts("down");

            break;
        case 'C':
            // printf("ESC[1C"); // code for arrow right
            puts("RIGHT");

            break;
        case 'D':
            // printf("ESC[1D"); // code for arrow left
            puts("LEFT");

            break;
        }
    }
}

// void start_key_control() {
//     int ch;
//     while (1) {
//         ch = get_ch();
//         if (ch == 'q' || ch == 'Q') {
//             break;
//         } else if (ch == '\r') {
//             enter_ptr();
//         } else if (ch == '\33') {
//             //在Linux终端读取^[为ESC，用'\33'表示(八进制)
//             ch = getchar();
//             if (ch == '[') {
//                 ch = getchar();
//                 switch (ch) {
//                     case 'A':
//                         up_ptr();
//                         break;
//                     case 'B':
//                         down_ptr();
//                         break;
//                     case 'C':
//                         right_ptr();
//                         break;
//                     case 'D':
//                         left_ptr();
//                         break;
//                     default:
//                         break;
//                 }
//             }
//         }
//     }
// }

// int getch(unsigned char* ch) {
//     return read(0, ch, sizeof(*ch));
// }

// void *blabla(void *p){
//     setup();
//      for(;;) {
//         // Busy-waiting on input
//         int retval;

//         // @brief Why use select?
//         //   Select checks if a system call will block. This makes it possible to only
//         //    do system calls when we know they will not block, even with a single threaded application.
//         //    If a lot of select calls is too expensive, we might just have a separate thread blocking.
//         //    If we use a reactive design, where we subscribe to thread-events, this will require a lot
//         //    less computation to pull off. For now I will just leave this commented out, since this app
//         //    can hapilly block for all I care... :P
//         //
//         //while ((retval = !select(1, &app.fileDescriptorSet, NULL, NULL, NULL)));   // @doc select() - http://man7.org/linux/man-pages/man2/select.2.html;

//         if (retval == -1) {
//             perror("select()...");
//         }
//         // read input
//         {
//             ssize_t count;
//             unsigned char ch;
//             count = getch(&ch);
//             printf("%c", ch); /* consume the character */
//             if(ch == 'q') break;
//             handleInput(&ch);
//         }
//     }
//     closeTerminalApp();
//     return 0;
// }

int keyboard_hit()
{
    struct timeval tv = {0L, 0L};
    // clear and set file descriptor
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);

    // will inspect only stdin
    return select(1, &fds, NULL, NULL, &tv);
}

void exit_handle()
{
    // kill networking thread if it exists
    // if (exit_flag_thread_listening)
    // {
    // 	pthread_cancel(message_manager);
    // }
    // if (exit_flag_connected)
    // {
    // 	shutdown(server_sock, SHUT_RDWR);
    // 	close(server_sock);
    // }

    // exit
    CLEAR_SCREEN();
    printf("\n   --- Disconnected ---\n\n");
    exit(0);
}

int main()
{
    pthread_t t;
    unsigned char temp;
    signal(SIGINT, exit_handle); // then why the check in the while(1)?
    signal(SIGTERM, exit_handle);
    // pthread_create(&t, NULL, blabla,NULL);
    // connect
    // exit_flag_connected = true;
    // pthread_create(&message_manager, 0, message_handler, 0); // (read messages from server)
    exit_flag_thread_listening = true;
    while (1)
    {
        if (keyboard_hit())
        {
            getch(&temp);
            // if (temp == SIGINT)
            if (temp == 3)
            {
                exit_handle();
            }

            //             //在Linux终端读取^[为ESC，用'\33'表示(八进制)
            //             ch = getchar();
            //              {
            //                 ch = getchar();
            //                 switch (ch) {
            //                     case 'A':
            //                         up_ptr();
            //                         break;
            //                     case 'B':

            // ansi sequences
            if (temp == '\33')
            {
                getch(&temp);; // ignored this one
                if (temp == '[')
                {
                    getch(&temp); // keep this one
                    if (temp == 'A')
                    {
                        puts("UP");
                    }
                    else if (temp == 'B')
                    {
                        puts("DOWN");
                    }
                    else if (temp == 'D')
                    {
                        puts("LEFT");
                    }
                    else if (temp == 'C')
                    {
                        exit(-1);
                        puts("RIGHT");
                    }
                }
            }
        }
        // sleep(10);
    }
    return 0;
}
#include "termios2.h"
struct termios orig_termios;
void *start_keyboard_listener(void *args)
{
    synchronize_info *info = (synchronize_info *)args;
    char *move = info->movement;

    int key;
    // setvmove(stdout, NULL, _IONBF, 0);
    signal(SIGINT, reset_terminal_mode); // then why the check in the while(1)?
    signal(SIGTERM, reset_terminal_mode);
    signal(SIGQUIT, reset_terminal_mode);
    signal(SIGUSR1, reset_terminal_mode);
    
    // printf("press a key: ");
    // fflush(stdout);

    set_conio_terminal_mode();
    bool getOut = false;
    while (!getOut)
    {

        // 27, 91, 66
        if (kbhit())
        {
            if (*info->isGameOver){
                getOut = true;
                break;
            }
            if (getch() == 27)
            {
                if (getch() == 91)
                {
                    pthread_mutex_lock(info->pc.lock);
                    // printf("got into switch!\r\n");
                    key=getch();
                    // printf("%d\r\n",key);
                    switch (key)
                    { // the real value
                    case 'A':
                        // printf("ESC[1A"); // code for arrow up
                        move[0] = UP;
                        break;
                    case 'B':
                        // printf("ESC[1B"); // code for arrow down
                        move[0] = DOWN;
                        break;
                    case 'C':
                        // printf("ESC[1C"); // code for arrow right
                        move[0] = RIGHT;
                        break;
                    case 'D':
                        // printf("ESC[1D"); // code for arrow left
                        move[0] = LEFT;
                        break;
                    case 'F':
                        getOut = true; // End
                        move[0] = STOPPED;
                        break;
                    }
                    pthread_cond_signal(info->pc.cond);
                    pthread_mutex_unlock(info->pc.lock);
                    // fflush(stdout);
                }
            }
        }
    }
    reset_terminal_mode();
    return (void *)(NULL);
}
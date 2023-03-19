#include <signal.h>
#include <pthread.h>

typedef struct pthread_condition
{
    pthread_cond_t *cond;
    pthread_mutex_t *lock;
} pthread_condition;

typedef struct synchronize_info{
    pthread_condition pc;
    char *movement;
    bool *isGameOver;
} synchronize_info;
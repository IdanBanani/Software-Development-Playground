#include <unistd.h>
// #include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <stdio.h>


static int x = 0;
static int b = 0;

void my_action(int signum, siginfo_t *info, void *ptr){
    x++;
    b++;    
    return;
}
void f(int signum){
    x++;    
    return;
}


int main(){
    struct sigaction action;
    struct sigaction old_action;
    // action.sa_handler = f; 
    action.sa_flags |= SA_SIGINFO;
    action.sa_sigaction = my_action; 
    sigaction(SIGALRM,&action,&old_action);
    
/*
ITIMER_REAL -decrements in real time, and delivers SIGALRM upon expiration.

ITIMER_VIRTUAL -
decrements only when the process is executing, and delivers SIGVTALRM upon expiration.

ITIMER_PROF
decrements both when the process executes and when the system is executing on behalf of the
 process. Coupled with ITIMER_VIRTUAL, this timer is usually used to profile the
  time spent by the application in user and kernel space.
 SIGPROF is delivered upon expiration.
*/
    // int setitimer(int which, const struct itimerval *new_value,
    //           struct itimerval *old_value);
    struct itimerval timer_val;
    struct timeval tv;
    struct timeval interval;
    tv.tv_usec = FIRST_TIMER;
    interval.tv_usec = INTERVAL_TIMER;

    timer_val.it_value = tv;
    timer_val.it_interval = interval;

    setitimer(ITIMER_REAL,&timer_val,NULL);    
    while(1){
        printf("%d %d sigalarm: %d\n",x,b,SIGALRM);
        usleep(FIRST_TIMER);
    }
    return 0;
}
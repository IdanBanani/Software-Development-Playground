/////////    error codes //////////////////
// Could have use macros from <winerror.h> for built in types.
#define VESSEL_ID_ERROR 2
#define NUM_VESSELS_ERROR 3
#define MEMORY_ALLOCATION_ERROR 4
#define PIPE_CREATION_ERROR 5
#define PROCESS_CREATION_ERROR 6
#define PIPE_USAGE_ERROR 7
#define THREAD_CREATION_ERROR 8
#define UNKNOWN_ANSWER_CODE_ERROR 8
#define MUTEX_RELEASE_ERROR 9
#define MUTEX_CREATION_ERROR 10
#define SEMAPHORE_CREATION_ERROR 11
#define SEMAPHORE_RELEASE_ERROR 12
#define PIPE_HANDLE_ERROR 13
#define INVALID_CRANE_IDX_ERROR 14
#define OPEN_MUTEX_ERROR 15
//////////////////////////////////////////

// CONSTANTS ////////////////////////////
#define MIN_VESSELS 2
#define MAX_VESSELS 50
#define PIPE_BUF_LEN 50
#define OUTPUT_BUFFER_LEN 256

//random time between 5 and 3000 msec between actions
#define MIN_SLEEP_TIME 5
#define MAX_SLEEP_TIME 3000

#define BUFSIZE 4096 //4KB- Up till this size, we are guaranteed that writes to pipe will be atomic

#define INVALID_VESSEL_ID (-1)

// pipe communication codes
#define VESSELS_NOT_ALLOWED "N"
#define VESSELS_ALLOWED "Y"
#define DONE_INDICATOR '#'
#define PRINT_INDICATOR '$'

#define PRINT_MUTEX "printingMutex"
////////////////////////////////////////

SYSTEMTIME timeToPrint;

//generic function to print system time information
const char* getTimeString()
{
	GetLocalTime(&timeToPrint);
	static char currentLocalTime[20];
	sprintf(currentLocalTime, "%02d:%02d:%02d", timeToPrint.wHour, timeToPrint.wMinute, timeToPrint.wSecond);
	return currentLocalTime;
}

void printToLog(char* msg) {
	// Note that we use stderr here in order to print to the shared parent console
	// printing to stdout won't work (see the redirections at initializePipesAndSI() )
	fprintf(stderr, "[%s] %s\n", getTimeString(), msg);
	return;
}


//generic function to randomise a Sleep time between MIN_SLEEP_TIME and MAX_SLEEP_TIME msec
int randSleepTime() {
	int sleepTime = rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME) + MIN_SLEEP_TIME;
	return sleepTime;
}

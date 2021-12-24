#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include <wait.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>

#define N 5          // Matrix size
#define FIN_PROB 0.1 // Probability of finishing (getting out of the circle)
#define NUM_OF_DIRECTIONS 4
#define SECONDS_TO_MICROSECONDS 1000000
// Car generation time values
#define MIN_INTER_ARRIVAL_IN_NS 8000000 // Minimum inter-arrival time in nanoseconds
#define MAX_INTER_ARRIVAL_IN_NS 9000000 // Maximum inter-arrival time in nanoseconds
#define INTER_MOVES_IN_NS 100000  // A car tries to progress to the next
                                  // square every INTER_MOVES_IN_NS [ns].

#define NUM_OF_BOARD_SNAPSHOTS 10 // Number of board snapshots to be taken
#define SIM_TIME 2 // The simulation takes SIM_TIME seconds.
#define NUM_OF_CAR_GENERATORS 4 // Number of car generators

#define MAX_NUMBER_OF_CARS 1000 // Maximum number of cars in the system
#define INNER_CELL_SYMBOL '@' // Symbol for the inner cells (not part of the circle, can't drive there) 
#define EMPTY_CELL ' '     
#define OCCUPIED_CELL '*' // The car symbol

#define BUFF_SIZE 200 // Buffer size for printing log messages to the console in bytes

//functions prototypes
void create_circle_mutexeses(pthread_mutex_t mutex_mat[N][N]);
void carGenerator(int generatorId);
void atomicPrint(char *str);
void lockMutexWithErrorCheck(pthread_mutex_t *mutex);
bool isOnBorder(int row, int col);
void releaseResources();

typedef enum
{
    RIGHT,
    UP,
    LEFT,
    DOWN
} Direction;

typedef enum
{
    UPPER_RIGHT,
    UPPER_LEFT,
    BUTTOM_LEFT,
    BUTTOM_RIGHT,
    EMPTY
} Corner;

typedef struct car
{
    int carID;             // Sequence number of the car (for debugging)
    int row;
    int col;
    int generator_id;      // id of the generator that created the car
    Direction dir;         // current direction of the car
    bool hasEnteredCircle; // has the car entered the circle already?
    bool isOnExitCorner;   // should the car check if it should exit on the next move?
    Corner corner;         // the last corner that the car came from
    int move;              // sequence number of the move
} Car;


// Static Global variables - shared between all functions in the program and the threads)
static int carID = 0;                                   // to keep track of the each created car.
static pthread_mutex_t cellsMutexes[N][N];              // mutexes for all the cells,
static Car *board[N][N] = {NULL};                       // Traffic circle matrix
static pthread_t car_threads[MAX_NUMBER_OF_CARS] = {0}; // Car threads array, for later joining the threads
static Car *cars[MAX_NUMBER_OF_CARS] = {0};             // Cars pointers array
static pthread_t car_generators[NUM_OF_CAR_GENERATORS]; // Car generators array

static pthread_mutex_t log_messages_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for the log messages
static pthread_mutex_t board_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for the board matrix status

static pthread_mutex_t print_board_mutex = PTHREAD_MUTEX_INITIALIZER; // in case more than one thread wants to 
                                                                      // print the board (not really needed here)

// TODO: check if can be moved into car_genarator function (make it a static variable inside the function)
static pthread_mutex_t carIDMutex = PTHREAD_MUTEX_INITIALIZER;

void atomicPrint(char *str)
{
    lockMutexWithErrorCheck(&log_messages_mutex);
    printf("%s", str);
    pthread_mutex_unlock(&log_messages_mutex);
}

bool shouldExitCircle(double probability)
{
    return rand() < probability * ((double)RAND_MAX + 1.0);
}

void lockMutexWithErrorCheck(pthread_mutex_t *mutex)
{
    int status = pthread_mutex_lock(mutex);
    if (status == EDEADLK)
    {
        perror("ERROR: Deadlock detected in the mutex\n");
        releaseResources();
        exit(EXIT_FAILURE);
    }
    else if (status == EINVAL)
    {
        perror("ERROR: Invalid mutex\n");
        releaseResources();
        exit(EXIT_FAILURE);
    }
    else if (status == EAGAIN)
    {
        perror("ERROR: Mutex is already locked\n");
        return;//we were told not to exit (even though we are not sure if it is the right thing to do)
    }
    else if (status == EPERM)
    {
        perror("ERROR: The current thread does not own the mutex\n");
        releaseResources();
        exit(EXIT_FAILURE);
    }
    else if(status == EBUSY)
    {
        perror("ERROR: The mutex is locked by another thread\n");
        releaseResources();
        exit(EXIT_FAILURE);
    }
    else if (status != 0){
        fprintf(stderr, "pthread_mutex_lock failed for general reason: %s\n", strerror(errno));
        releaseResources();
        exit(status);
    }
    return;
}

void initAllCellsMutexes()
{
    int return_val;
    for (int i = 0; i < N; i++)
    {
        for (int j = 0; j < N; j++)
        {
            if ((return_val = pthread_mutex_init(&cellsMutexes[i][j], NULL)) != 0)
            {
                fprintf(stderr, "Error creating mutex: %s\n", strerror(errno));
                releaseResources();
                exit(return_val);
            }
        }
    }
    return;
}

Corner getCornerType(int row, int col)
{
    if (row == 0 && col == 0)
        return UPPER_LEFT;
    else if (row == 0 && col == N - 1)
        return UPPER_RIGHT;
    else if (row == N - 1 && col == 0)
        return BUTTOM_LEFT;
    else if (row == N - 1 && col == N - 1)
        return BUTTOM_RIGHT;
    else
        return EMPTY;
}

// printing the traffic circle
void printBoard()
{
    char symbol;
    lockMutexWithErrorCheck(&print_board_mutex);
    lockMutexWithErrorCheck(&board_mutex);
    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < N; ++j)
        {
            if (!isOnBorder(i, j))
            {
                symbol = INNER_CELL_SYMBOL;
            }
            else
            {
                if (!board[i][j])
                    symbol = EMPTY_CELL;
                else
                    symbol = OCCUPIED_CELL;
            }
            printf("%c", symbol);
        }
        printf("\n");
    }
    printf("\n");
    pthread_mutex_unlock(&board_mutex);
    pthread_mutex_unlock(&print_board_mutex);
    return;
}

void printBoardSnapshots()
{
    // Would have been a better idea to define period of taking snapshots (snapshot every X seconds)
    // But it wasn't part of the instructions.
    for (int i = 0; i < NUM_OF_BOARD_SNAPSHOTS; i++)
    {
        printBoard(board);
        usleep((SIM_TIME / (double)(NUM_OF_BOARD_SNAPSHOTS)) * SECONDS_TO_MICROSECONDS); // convert to microseconds
    }
    return;
}

bool isOnExitCorner(int row, int col)
{
    return (row == 0 && col == 0) || (row == 0 && col == N - 1) || (row == N - 1 && col == 0) || (row == N - 1 && col == N - 1);
}

bool isOnBorder(int row, int col)
{
    return row == 0 || row == N - 1 || col == 0 || col == N - 1;
}


void getNextRowCol(int *row, int *col, Direction dir, int *nextRow, int *nextCol)
{
    int new_row = *row;
    int new_col = *col;
    switch (dir)
    {
    case UP:
        new_row--;
        break;
    case RIGHT:
        new_col++;
        break;
    case DOWN:
        new_row++;
        break;
    case LEFT:
        new_col--;
        break;
    default:
        exit(-1);
    }
    *nextRow = new_row;
    *nextCol = new_col;
}

// what extra cell indexes do we need to check before entering the circle?
void cellIndexesToCheck(Corner corner, int *row, int *col)
{
    switch (corner)
    {
    case UPPER_LEFT:
        *row = 0;
        *col = 1;
        break;
    case UPPER_RIGHT:
        *row = 1;
        *col = N - 1;
        break;
    case BUTTOM_LEFT:
        *row = N - 2;
        *col = 0;
        break;
    case BUTTOM_RIGHT:
        *row = N - 1;
        *col = N - 2;
        break;
    case EMPTY:
    default:
        exit(-1);
    }
}

void updateBoardAndCar(Car *car, int nextRow, int nextCol, int currRow, int currCol)
{
    lockMutexWithErrorCheck(&board_mutex);
    board[nextRow][nextCol] = car;    // update board
    
    if (car->hasEnteredCircle){
        board[currRow][currCol] = NULL; // remove car from old position if it has entered the circle before
    }
    pthread_mutex_unlock(&board_mutex);
    
    if (isOnExitCorner(nextRow, nextCol) && car->hasEnteredCircle)
    {
        car->isOnExitCorner = true;
        car->dir = (car->dir + 1) % NUM_OF_DIRECTIONS; // change direction
        car->corner = getCornerType(nextRow, nextCol); // for debug
    }
    else
    {
        car->isOnExitCorner= false; // don't try to exit circle/change direction
    }
    car->col = nextCol;               // update car position
    car->row = nextRow;
    return;
}

void moveCar(Car *car)
{   
    if (!car)
    {
        perror("moveCar: car is NULL");
        releaseResources();
        exit(EXIT_FAILURE);
    }
    while (true)
    {
        int currentRow = car->row;
        int currentCol = car->col;
        int nextRow, nextCol;
        int rowToTest, colToTest;

        if (!car->hasEnteredCircle)
        {
            getNextRowCol(&(car->row), &(car->col), car->dir, &nextRow, &nextCol);
            cellIndexesToCheck(car->corner, &rowToTest, &colToTest);
            
            //try to enter the circle
            lockMutexWithErrorCheck(&cellsMutexes[rowToTest][colToTest]);
            lockMutexWithErrorCheck(&cellsMutexes[nextRow][nextCol]);

            //enter the circle, update board and car
            updateBoardAndCar(car, nextRow, nextCol, currentRow, currentCol);
            car->hasEnteredCircle = true;
            
            pthread_mutex_unlock(&cellsMutexes[rowToTest][colToTest]);
        }
        else
        {
            if (car->isOnExitCorner && shouldExitCircle(FIN_PROB))
            {
                    // car is exiting the circle
                    cars[car->carID] = NULL; // to prevent double free later
                    lockMutexWithErrorCheck(&board_mutex);
                    board[car->row][car->col] = NULL; // remove from the board
                    pthread_mutex_unlock(&board_mutex);
                    pthread_mutex_unlock(&cellsMutexes[currentRow][currentCol]); 
                    free(car); // free the car memory
                    car_threads[car->carID] = 0; //TODO: change car_threads to array of pointer
                    pthread_exit(NULL); //or use return;
            }
            else
            {
                // Continue in the circle
                getNextRowCol(&(car->row), &(car->col), car->dir, &nextRow, &nextCol);
                lockMutexWithErrorCheck(&cellsMutexes[nextRow][nextCol]); // wait for next cell to be empty
                updateBoardAndCar(car, nextRow, nextCol, currentRow, currentCol);
                pthread_mutex_unlock(&cellsMutexes[currentRow][currentCol]); 
            }
            
        }
        usleep(INTER_MOVES_IN_NS / 1000.0); // sleep for a while
    }
    return;
}

void initCar(Car *p_car, int generatorId, int carID)
{
    switch (generatorId)
    {
    case 0:
        p_car->row = N - 1;
        p_car->col = -1;
        p_car->dir = RIGHT;
        p_car->corner = BUTTOM_LEFT;
        break;
    case 1:
        p_car->row = N;
        p_car->col = N - 1;
        p_car->dir = UP;
        p_car->corner = BUTTOM_RIGHT;
        break;
    case 2:
        p_car->row = 0;
        p_car->col = N;
        p_car->dir = LEFT;
        p_car->corner = UPPER_RIGHT;
        break;
    case 3:
        p_car->row = -1;
        p_car->col = 0;
        p_car->dir = DOWN;
        p_car->corner = UPPER_LEFT;
        break;
    }
    p_car->move = 0;
    p_car->generator_id = generatorId;
    p_car->carID = carID;
    p_car->isOnExitCorner = false;  //not allowed to exit circle at the beginning
    p_car->hasEnteredCircle = false;
}


// car generator function
void carGenerator(int generatorId)
{
    int rand_interval; // random time interval between car generations
    srand(time(NULL)); // pseudo-random number generator seed initialization with current time
                       //  (seed is time(NULL),which is the current time->different results every run of the program)

    while (true)
    {
        // generate a random number between min and max
        rand_interval = rand() % (MAX_INTER_ARRIVAL_IN_NS - MIN_INTER_ARRIVAL_IN_NS + 1) + MIN_INTER_ARRIVAL_IN_NS;
        usleep(rand_interval / 1000.0); // Convert nsec to usec
        Car *p_car = malloc(sizeof(*p_car)); // allocate memory for the car
        if (!p_car)
        {
            perror("car generator: malloc failed");
            releaseResources();
            exit(EXIT_FAILURE);
        }

        lockMutexWithErrorCheck(&carIDMutex);
        initCar(p_car, generatorId, carID++);
        pthread_mutex_unlock(&carIDMutex);

        cars[p_car->carID] = p_car; // add car to the cars array

        int status = pthread_create(&car_threads[p_car->carID], NULL, (void *)moveCar, p_car);
        if (status != 0)
        {
            perror("car generator: pthread_create error");
            releaseResources();
            exit(status);
        }

    }
}

void releaseResources()
{
    // cancel all car generator threads
    for (int i = 0; i < NUM_OF_CAR_GENERATORS; i++)
    {
        pthread_cancel(car_generators[i]);
    }

    for (int i = 0; i < carID; ++i)
    {
        free(cars[i]); // free the cars which are not already freed (!=NULL)
        if (car_threads[i])
        {
            pthread_cancel(car_threads[i]);
        }
    }
    
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            pthread_mutex_destroy(&cellsMutexes[i][j]);

    pthread_mutex_destroy(&board_mutex);
    pthread_mutex_destroy(&log_messages_mutex);
    pthread_mutex_destroy(&print_board_mutex);
    pthread_mutex_destroy(&carIDMutex);
}

int main()
{
    pthread_t print_board_thread;                    // Print board thread
    pthread_mutex_init(&log_messages_mutex, NULL);
    pthread_mutex_init(&board_mutex, NULL);
    pthread_mutex_init(&print_board_mutex, NULL);
    initAllCellsMutexes(); // initialize all cells mutexes

    // create board print thread
    pthread_create(&print_board_thread, NULL, (void *)printBoardSnapshots, NULL);

    // create car generators threads
    for (size_t i = 0; i < NUM_OF_CAR_GENERATORS; i++)
    {
        pthread_create(&car_generators[i], NULL, (void *)carGenerator, (void *)i);
    }

    // wait for board print thread to finish
    pthread_join(print_board_thread, NULL);
    releaseResources();

    return 0;
}
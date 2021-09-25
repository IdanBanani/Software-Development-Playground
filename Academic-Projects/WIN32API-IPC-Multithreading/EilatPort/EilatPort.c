#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <windows.h>
#include "protocol.h"

#define CRANE_AVAILABLE (-1)
#define INVALID_CRANE_IDX (-1)
#define MIN_CARGO_WEIGHT 5
#define MAX_CARGO_WEIGHT 50

static char readPipeBuffer[PIPE_BUF_LEN];

static HANDLE WriteHandle, ReadHandle;
static DWORD bytesSent, bytesRecieved;
static DWORD WINAPI Vessel(PVOID); //used for creating Vessel thread
static DWORD WINAPI Crane(PVOID);  //used for creating Crane thread
static HANDLE* Cranes;

static int num_of_cranes = 0;
static int cranesWithVessels = 0;
static int* craneIDsArr;
static int* cranesVessleIDs;
static int* cargoWeights;

static int numOfVessels, vesselsInBarrier = 0;
static bool isQuayADTFree = true;
static int* vesselThreadsIDArr;
static HANDLE* vesselThreads;

static DWORD ThreadId;

static HANDLE sharedPrintMutex;
static HANDLE writePipeMutex, mutexAdt;

// TODO: Is there a better way to conrtol acception of a vessel to a crane and releasing it from it?
static HANDLE* cranesOutSemaphores; //For synchronization between cranes and quay leaving vessels
static HANDLE* cranesInSemaphores; //For synchronization between cranes and vessels entering the unloading quay

static HANDLE barrierMutex;		//For defending the barrier data-structure

static HANDLE* barrierQueue; // Array of semaphores for the barrier queue
							 // Possible space optimization for large numOfVessels: Use a linked list instead of an array

static int barrierTaillIdx = -1; //Last entered vessel array index of the barrier fifo
static int barrierHeadIdx = 0; // First in the barrier fifo to release next

//Functions prototypes
bool isPrime(int);
void checkNumOfVessels();
int generateNumberOfCranes(int);
void initStaticVars();
void logVesselAtBarrier(int);
void setPipesHandles();
void receiveVesselsFromHaifa();
void enterToBarrier(int);
void releaseFromBarrier();
int enterUnloadingQuay(int);
void leaveQuay(int, int);
void sailBackToHaifa(int);
void waitForCleanUpCondition();
void releaseResources();
void consolePrint(char*);
void exitWithError(const char* msg, int errorCode);


// Print error to mutual console , cleanup resources and exit
void exitWithError(const char* msg, int errorCode) {
	WaitForSingleObject(sharedPrintMutex, INFINITE);

	fprintf(stderr, "[%s] Error - %s (%d)\n", getTimeString(), msg, GetLastError());
	if (!ReleaseMutex(sharedPrintMutex)) {
		fprintf(stderr, "Error: Failed to release printing mutex\n %d \n", GetLastError());
		exit(MUTEX_RELEASE_ERROR);
	}
	releaseResources();
	exit(errorCode); // ExitProcess accepts an unsigned int exit code (unlink exit(int))
}


//Thread function for each Crane
DWORD WINAPI Crane(PVOID Param)
{
	//Get the Unique ID from Param and keep it in a local variable
	int craneID = *(int*)Param;
	char strBuffer[OUTPUT_BUFFER_LEN];
	sprintf(strBuffer, "Crane - %d has been created", craneID);
	consolePrint(strBuffer);

	unsigned int vesselsPerCrane = numOfVessels / num_of_cranes;

	//Every Crane will serve a total of vesselsPerCrane vessels one at a time
	for (unsigned int i = 0; i < vesselsPerCrane; i++)
	{
		// Defends against Doing a Crane job until next time barrier releases vessels
		WaitForSingleObject(cranesInSemaphores[craneID - 1], INFINITE);

		int vesselID = cranesVessleIDs[craneID - 1];

		sprintf(strBuffer, "vessel %d - Starts unloading  %d Tons at Crane %d...", vesselID, cargoWeights[craneID - 1], craneID);
		consolePrint(strBuffer);

		Sleep(randSleepTime());

		sprintf(strBuffer, "vessel %d - Finished unloading cargo of weight %d Tons at Crane %d", vesselID, cargoWeights[craneID - 1], craneID);
		consolePrint(strBuffer);

		//only now release vessel from crane
		if (!ReleaseSemaphore(cranesOutSemaphores[craneID - 1], 1, NULL))
			exitWithError("UnloadingQuay::Unexpected error cranesOutSemaphores.V()", SEMAPHORE_RELEASE_ERROR);
	}

	return 0; // Indication value for the related vessel that the unloading is done successfuly 
}


//Thread function for each Vessel
DWORD WINAPI Vessel(PVOID Param)
{
	//a unique Vessel ID equal to the one given in Haifa port
	int vesselID = *((int*)Param);

	// Before entering the Crane, The vessel need to wait at the barrier
	enterToBarrier(vesselID);

	Sleep(randSleepTime()); // simulate work

	int craneIdx = enterUnloadingQuay(vesselID);

	//Error Handling
	if (craneIdx == INVALID_CRANE_IDX)
	{
		exitWithError("Unexpected Error Thread %d Entering Crain", INVALID_CRANE_IDX_ERROR);
	}

	// need to wait for crane to finish it's job.
	WaitForSingleObject(cranesOutSemaphores[craneIdx], INFINITE);

	leaveQuay(vesselID, craneIdx); //release the vessel from the crane & quay

	sailBackToHaifa(vesselID); //report sailing back and sail
	return 0; //success ,finish the thread life

}


bool isPrime(int numOfVessels)
{
	for (int i = 2; i <= (int)sqrt(numOfVessels); i++)
	{
		if (numOfVessels % i == 0)
		{
			return false;
		}
	}
	return true;
}

// We need a random number of cranes such that:
// #vessels % #cranes == 0    and 1 < #cranes < #vessels
int generateNumberOfCranes(int numOfVessels) {
	int numOfCranes;
	while (true)
	{
		//possible matching values are in range [2 , numOfVessels/2]
		numOfCranes = rand() % ((numOfVessels / 2) - 1) + 2;
		if (numOfVessels % numOfCranes == 0)
		{
			return numOfCranes;
		}
	}
}

//Vessel has finished unloading in Eilat,and returns to back to Haifa.
void sailBackToHaifa(int vesselID)
{

	char strBuffer[OUTPUT_BUFFER_LEN];
	sprintf(strBuffer, "Vessel %d - entering Canal: Red Sea ==> Med. Sea ", vesselID);
	consolePrint(strBuffer);

	char tempWriteBuff[PIPE_BUF_LEN];
	sprintf(tempWriteBuff, "%d", vesselID);

	Sleep(randSleepTime()); // sleep to simulate sailing to Haifa

	// send the vessel through the cannal to Haifa
	WaitForSingleObject(writePipeMutex, INFINITE);
	/* Critical section start */
	if (!WriteFile(WriteHandle, tempWriteBuff, PIPE_BUF_LEN, &bytesSent, NULL))
		exitWithError("Eilat ::Error writing to pipe", PIPE_USAGE_ERROR);
	/* Critical section end */
	if (!ReleaseMutex(writePipeMutex))
		exitWithError("Eilat :: Unexpected error mutexPipe.V()", MUTEX_RELEASE_ERROR);
}


// A traffic passage flow control point for the vessels
// The Barrier is implemented as a queue (represented by a dynamic array)
// of binary semaphores [max queue size is equal to its capacity which is numOfVessels] 
void enterToBarrier(int vessel_ID)
{
	char strBuffer[OUTPUT_BUFFER_LEN];

	/* This is a critical point! (deadlock danger)
		Even though logically we might think we can postpone the lock  of
		mutexAdt till dealing with isQuayADTFree,
		If we dont lock in the following order (SAME as the one in other functions)
		We will have a deadlock =>Conclusion: Grab(Lock) multiple locks in the same order
		We need two locks because the barrier and the ADT are dependent on each other's state
		for some of their actions */
	WaitForSingleObject(mutexAdt, INFINITE);
	WaitForSingleObject(barrierMutex, INFINITE);

	vesselsInBarrier++;
	int barrierTaillIdxSnapshot = ++barrierTaillIdx; // the static var value might be changed after releasing barrier mutex
	logVesselAtBarrier(vessel_ID);

	// Now there is e chance that there are enough vessels in barrier:
	// If the unloading quay is empty  AND There are at least numOfCranes vessels in the barrier:
	//   then release the group from the barrier
	if (isQuayADTFree && vesselsInBarrier >= num_of_cranes) {
		releaseFromBarrier(); // we have enough vessels at the barrier and unloading Quay is free
	}

	/* Order of unlocking is less important here and won't create a deadlock
		But we prefer releasing in the reverse order */
	if (!ReleaseMutex(barrierMutex))
		exitWithError("EilatPort :: Unexpected error while trying to release barrier mutex", MUTEX_RELEASE_ERROR);

	if (!ReleaseMutex(mutexAdt))
		exitWithError("EilatPort :: Unexpected error while trying to release mutexAdt", MUTEX_RELEASE_ERROR);


	// Wait to enter the Unloading quay 
	WaitForSingleObject(barrierQueue[barrierTaillIdxSnapshot], INFINITE);

	/// Enter critical section
	WaitForSingleObject(barrierMutex, INFINITE);
	vesselsInBarrier--;
	if (!ReleaseMutex(barrierMutex))
		exitWithError("EilatPort :: Unexpected error while trying to release barrier mutex", MUTEX_RELEASE_ERROR);
	/// Exit critical section

	sprintf(strBuffer, "Vessel %d - Released from barrier to unloading quay", vessel_ID);
	consolePrint(strBuffer);
}

//Function for process of Vessel with vesselID entry to the unloadingQuay.
//it settles down at an available Crane for unloading and wakes up the relevant crane.
int enterUnloadingQuay(int vessel_ID)
{

	char strBuffer[OUTPUT_BUFFER_LEN];
	int craneIdx = INVALID_CRANE_IDX;

	/* Enter critical section */
	WaitForSingleObject(mutexAdt, INFINITE);

	//Find an unoccupied(idle=free) crane for this vessel
	for (craneIdx = 0; craneIdx < num_of_cranes; craneIdx++)
		if (cranesVessleIDs[craneIdx] == CRANE_AVAILABLE)
		{
			cranesVessleIDs[craneIdx] = vessel_ID; //update cell with the vessel id
			cranesWithVessels++;				//we need to know when all cranes have finished
			break;						// done searching
		}

	Sleep(randSleepTime()); //simulate time to enter unloading quay

	sprintf(strBuffer, "Vessel %d - settled down at Crane %d", vessel_ID, craneIdx + 1);
	consolePrint(strBuffer);

	cargoWeights[craneIdx] = rand() % (MAX_CARGO_WEIGHT - MIN_CARGO_WEIGHT) + MIN_CARGO_WEIGHT;

	sprintf(strBuffer, "Vessel %d - weight is %d tons", vessel_ID, cargoWeights[craneIdx]);
	consolePrint(strBuffer);

	if (!ReleaseMutex(mutexAdt)) {
		exitWithError("UnloadingQuay::Unexpected error mutex.V()", MUTEX_RELEASE_ERROR);
	}
	/* Exit critical section */

	// Let the crane (the consumer) know it has a job to do now
	if (!ReleaseSemaphore(cranesInSemaphores[craneIdx], 1, NULL))
		exitWithError("UnloadingQuay::Unexpected error cranesInSemaphores.V()", MUTEX_RELEASE_ERROR);

	/*
	Another confusing point:
	Releasing the crane (in order to start unloading the vessel cargo
	can be be done after releasing the adt mutex but it is just because
	the crane's thread work doesn't affect the Quay ADT state inside it,
	Even though the cranes are part of the quay ADT.
	No other vessel can enter the loading quay and settle at this crane
	until current vessel will be released from it. so we are safe to do it
	This design would probabely work for the case of each crane handling multiple vessels simultaniously
	(The order of execution won't be guranteed)
	*/

	return craneIdx;
}


//Departure of a vessel from the unloading quay and from a specific crane.
// (After crane has done unloading its cargo)
void leaveQuay(int vessel_ID, int craneIdx)
{
	char strBuffer[OUTPUT_BUFFER_LEN];
	Sleep(randSleepTime()); //for simulating exit from Quay ADT
	///////////////////////////////////////////////////////////////////////////
	WaitForSingleObject(mutexAdt, INFINITE); //we are accessing the quay adt structure
	cranesVessleIDs[craneIdx] = CRANE_AVAILABLE;
	cranesWithVessels--;
	cargoWeights[craneIdx] = 0; //reset

	sprintf(strBuffer, "Vessel %d - left Crane %d and Exiting the UnloadingQuay", vessel_ID, craneIdx + 1);
	consolePrint(strBuffer);

	if (cranesWithVessels == 0)
	{
		isQuayADTFree = true;
		//////////////////////////////////////////////////
		WaitForSingleObject(barrierMutex, INFINITE);

		if (vesselsInBarrier >= num_of_cranes)
		{
			releaseFromBarrier();
		}

		if (!ReleaseMutex(barrierMutex))
		{
			exitWithError("UnloadingQuay::Unexpected error when releasing barrierMutex", -1);
		}
		//////////////////////////////////////////////////
	}

	if (!ReleaseMutex(mutexAdt))
	{
		exitWithError("UnloadingQuay::Unexpected error when releasing ADT mutex", -1);
	}
	///////////////////////////////////////////////////////////////////////////
}


// release M=num_of_cranes vessels from the head of barrier queue
void releaseFromBarrier()
{
	char strBuffer[OUTPUT_BUFFER_LEN];

	sprintf(strBuffer, "EilatPort: Barrier is now releasing %d Vessels", num_of_cranes);
	consolePrint(strBuffer);

	// Allow the num_of_cranes vessels from head of queue to go to the unloading quay
	// (pops a vessel from the barrier queue head)
	for (int i = 0; i < num_of_cranes; i++)
	{
		if (!ReleaseSemaphore(barrierQueue[barrierHeadIdx++], 1, NULL)) {
			sprintf(strBuffer, "releaseFromBarrier:: Unexpected error  barrierQueue[%d].V()\n", barrierHeadIdx - 1);
			exitWithError(strBuffer, SEMAPHORE_RELEASE_ERROR);
		}
	}

	isQuayADTFree = false; //we have just sent vessels into the unloading quay ("ADT")
}

//Function to allocated & Initialise memory for static variables 
// prior to starting Vessels Threads (Crane threads are created here).
void initStaticVars() {

	writePipeMutex = CreateMutex(NULL, FALSE, NULL);
	if (writePipeMutex == NULL)
	{
		exitWithError("EilatPort: Unexpected Error in writePipeMutex creation", PIPE_USAGE_ERROR);
	}

	barrierMutex = CreateMutex(NULL, FALSE, NULL);
	if (barrierMutex == NULL)
	{
		exitWithError("EilatPort:Unexpected Error in barrierMutex Creation", MUTEX_CREATION_ERROR);
	}

	mutexAdt = CreateMutex(NULL, FALSE, NULL);
	if (mutexAdt == NULL)
	{
		exitWithError("EilatPort:Unexpected Error in mutexAdt Creation", MUTEX_CREATION_ERROR);
	}

	vesselThreadsIDArr = (int*)malloc(numOfVessels * sizeof(int));
	if (!vesselThreadsIDArr)
	{
		exitWithError("EilatPort: Error allocating vesselThreadsIDArr", MEMORY_ALLOCATION_ERROR);
	}

	cargoWeights = (int*)malloc(num_of_cranes * sizeof(int));
	if (cargoWeights == NULL)
	{
		exitWithError("EilatPort: Error allocating cargoWeights", MEMORY_ALLOCATION_ERROR);
	}

	vesselThreads = (HANDLE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (numOfVessels * sizeof(HANDLE)));
	if (vesselThreads == NULL)
	{
		exitWithError("EilatPort: Error allocating Vessels threads", MEMORY_ALLOCATION_ERROR);
	}

	cranesInSemaphores = (HANDLE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (num_of_cranes * sizeof(HANDLE)));
	if (cranesInSemaphores == NULL)
	{
		exitWithError("EilatPort: Error creating cranesInSemaphores", MEMORY_ALLOCATION_ERROR);
	}

	cranesOutSemaphores = (HANDLE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (num_of_cranes * sizeof(HANDLE)));
	if (cranesOutSemaphores == NULL)
	{
		exitWithError("EilatPort: Error creating cranesOutSemaphores", MEMORY_ALLOCATION_ERROR);
	}

	barrierQueue = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (numOfVessels * sizeof(HANDLE)));
	if (barrierQueue == NULL)
	{
		exitWithError("EilatPort: Error allocating barrierQueueArray", MEMORY_ALLOCATION_ERROR);
	}

	Cranes = (HANDLE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (num_of_cranes * sizeof(HANDLE)));
	if (Cranes == NULL)
	{
		exitWithError("EilatPort: Error allocating Cranes", MEMORY_ALLOCATION_ERROR);
	}

	craneIDsArr = (int*)malloc(num_of_cranes * sizeof(int));
	if (craneIDsArr == NULL)
	{
		exitWithError("EilatPort: Error allocating Cranes IDs", MEMORY_ALLOCATION_ERROR);
	}

	cranesVessleIDs = (int*)malloc(num_of_cranes * sizeof(int));
	if (cranesVessleIDs == NULL)
	{
		exitWithError("EilatPort: Error allocating Cranes array", MEMORY_ALLOCATION_ERROR);
	}

	for (int i = 0; i < num_of_cranes; i++)
	{
		cargoWeights[i] = 0;
		cranesVessleIDs[i] = CRANE_AVAILABLE;
		craneIDsArr[i] = i + 1; //unique sequence number starting from 1

		Cranes[i] = CreateThread(NULL, 0, Crane, &craneIDsArr[i], 0, &ThreadId);
		if (!Cranes[i])
		{
			char formattedStringBuff[OUTPUT_BUFFER_LEN];
			sprintf(formattedStringBuff, "EilatPort: Failed to create a thread for Crane %d", i);
			exitWithError(formattedStringBuff, THREAD_CREATION_ERROR);
		}
		cranesInSemaphores[i] = CreateSemaphore(NULL, 0, 1, NULL);
		cranesOutSemaphores[i] = CreateSemaphore(NULL, 0, 1, NULL);
	}

	for (int i = 0; i < numOfVessels; i++)
	{
		barrierQueue[i] = CreateSemaphore(NULL, 0, 1, NULL);;
		if (!barrierQueue[i])
		{
			exitWithError("EilatPort: Error when creating mutex for a barrier", SEMAPHORE_CREATION_ERROR);
		}
	}

	return;
}


//Function to clean global data and closing Threads after all Threads finish.
// It will be called only after both of these happend sequentialy:
// 1. All vessels in Eilat Port Finished their job
// 2. Unloading Quay Cranes have finished their life-cycle
void releaseResources()
{

	CloseHandle(mutexAdt);
	CloseHandle(barrierMutex);
	CloseHandle(writePipeMutex);
	CloseHandle(sharedPrintMutex);

	for (int i = 0; i < numOfVessels; i++)
	{
		CloseHandle(barrierQueue[i]);
	}

	for (int i = 0; i < num_of_cranes; i++)
	{
		CloseHandle(cranesInSemaphores[i]);
		CloseHandle(cranesOutSemaphores[i]);
		CloseHandle(Cranes[i]); // Cranes end their life only when main thread is done
	}

	for (int i = 0; i < numOfVessels; i++)
	{
		CloseHandle(vesselThreads[i]);
	}

	free(cargoWeights);
	free(cranesVessleIDs);
	free(vesselThreadsIDArr);
	free(craneIDsArr);

	HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, barrierQueue);
	HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, vesselThreads);
	HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, Cranes);
	HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, cranesInSemaphores);
	HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, cranesOutSemaphores);


}


// For synchronization between print requests (among both HaifaPort & EilatPort)
void consolePrint(char* msg)
{
	WaitForSingleObject(sharedPrintMutex, INFINITE);
	printToLog(msg);
	if (!ReleaseMutex(sharedPrintMutex)) {
		printf("PrintToConsole::Unexpected error mutex.V()\n"); //
		releaseResources();
		exit(MUTEX_RELEASE_ERROR);
	}
}


//The main thread is listening/waiting for numOfVessels messages from Haifa 
// (on an Anonymous pipe) and creating a Vessel thread for each of them ("Eilat vessel").
void receiveVesselsFromHaifa()
{
	int vessel_ID;
	int vesselsVisitedEilat = 0;
	char strBuffer[OUTPUT_BUFFER_LEN];
	while (vesselsVisitedEilat < numOfVessels)
	{

		if (!ReadFile(ReadHandle, readPipeBuffer, PIPE_BUF_LEN, &bytesRecieved, NULL))
		{
			exitWithError("Eilat ::Error Reading from pipe", PIPE_USAGE_ERROR);
		}

		sprintf(strBuffer, "Vessel %s - arrived @ Eilat Port ", readPipeBuffer);
		consolePrint(strBuffer);

		vessel_ID = atoi(readPipeBuffer);
		vesselThreadsIDArr[vessel_ID - 1] = vessel_ID;
		vesselsVisitedEilat++;

		//Create a thread to represent a vessel that is now in EilatPort
		// The thread will work "in parallel" to the main thread
		vesselThreads[vessel_ID - 1] = CreateThread(NULL, 0, Vessel, &vesselThreadsIDArr[vessel_ID - 1], 0, &ThreadId);

	}
}

// Wait for all Cranes & Vessels Threads to finish 
void waitForCleanUpCondition()
{
	WaitForMultipleObjects(numOfVessels, vesselThreads, TRUE, INFINITE);
	consolePrint("Eilat Port:  All Vessel Threads in Eilat are done ");

	// Wait for all Crane threads to finish
	WaitForMultipleObjects(num_of_cranes, Cranes, TRUE, INFINITE);
	consolePrint("Eilat Port: All Crane Threads are done ");
	return;
}


//Checking the correctness of the number of vessels and returning a response to Haifa Process.
void checkNumOfVessels()
{
	char tempWriteBuffer[PIPE_BUF_LEN];
	bool is_prime = isPrime(numOfVessels);
	if (!is_prime)
	{
		strcpy(tempWriteBuffer, VESSELS_ALLOWED);
	}
	else
	{
		strcpy(tempWriteBuffer, VESSELS_NOT_ALLOWED);
	}

	//Write to the pipe
	if (!WriteFile(WriteHandle, tempWriteBuffer, PIPE_BUF_LEN, &bytesSent, NULL))
		exitWithError("EilatPort: Error writing to pipe", PIPE_USAGE_ERROR);

	if (is_prime)
		exitWithError("EilatPort: number of vessels argument is prime!", NUM_VESSELS_ERROR);

	return;
}

//Initialization of pipes handles at Eilat side
void setPipesHandles()
{
	ReadHandle = GetStdHandle(STD_INPUT_HANDLE);   // get the read handle of pipe 1
	WriteHandle = GetStdHandle(STD_OUTPUT_HANDLE); // get the write handle of pipe 2

	if ((WriteHandle == INVALID_HANDLE_VALUE) || (ReadHandle == INVALID_HANDLE_VALUE))
		exitWithError("Eilat:: Unexpected error while setting pipes handles", PIPE_HANDLE_ERROR);
}

void logVesselAtBarrier(int vessel_ID)
{
	char strBuffer[OUTPUT_BUFFER_LEN];
	sprintf(strBuffer, "Vessel %d - Entered the Barrier", vessel_ID);
	consolePrint(strBuffer);
}


int main(int argc, char* argv[])
{
	//TODO: can you make this work as a printing method to the parent process console?
	//AttachConsole(ATTACH_PARENT_PROCESS);
	//printf("\n ** This is a message from the child process. ** \n");

	char strBuffer[OUTPUT_BUFFER_LEN];
	srand((unsigned int)time(NULL)); //initialize the PRNG seed with pc current time
	setPipesHandles();

	// since we are printing to a common console - we must 
	// synchronize the printings since the first one for this process
	sharedPrintMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, TEXT(PRINT_MUTEX));
	if (!sharedPrintMutex) {
		fprintf(stderr, "EilatPort: Error creating mutexPipe");
		exit(OPEN_MUTEX_ERROR);
	}

	// Read num of vessels HaifaPort is asking to send from the relevant pipe
	if (ReadFile(ReadHandle, readPipeBuffer, PIPE_BUF_LEN, &bytesRecieved, NULL)) {
		sprintf(strBuffer, "EilatPort: Received request message of - %s - Vessels", readPipeBuffer);
		consolePrint(strBuffer);
	}
	else {
		exitWithError("EilatPort: Error reading from pipe", PIPE_USAGE_ERROR);
	}

	numOfVessels = atoi(readPipeBuffer);

	checkNumOfVessels();

	num_of_cranes = generateNumberOfCranes(numOfVessels);
	sprintf(strBuffer, "EilatPort: Number of cranes is %d", num_of_cranes);
	consolePrint(strBuffer);

	initStaticVars();

	receiveVesselsFromHaifa();

	waitForCleanUpCondition();
	consolePrint("Eilat Port: Now Exiting... ");
	releaseResources();

	return 0;

}


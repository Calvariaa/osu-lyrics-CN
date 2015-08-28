#define WIN32_LEAN_AND_MEAN
#pragma comment (lib, "Shlwapi.lib")

#include <Windows.h>
#include <Shlwapi.h>
#include <unordered_map>
using namespace std;

#define BUF_SIZE MAX_PATH * 3

long long CurrentTime()
{
    long long t;
    GetSystemTimeAsFileTime((LPFILETIME) &t);
    return t;
}


BOOL unhook_by_code(LPCTSTR szDllName, LPCTSTR szFuncName, PBYTE pOrgBytes)
{
    FARPROC pFunc;
    DWORD dwOldProtect;

    // API �ּ� ���Ѵ�
    pFunc = GetProcAddress(GetModuleHandle(szDllName), szFuncName);

    // ���� �ڵ� (5 byte)�� ����� ���� �޸𸮿� WRITE �Ӽ� �߰�
    VirtualProtect((LPVOID) pFunc, 5, PAGE_EXECUTE_READWRITE, &dwOldProtect);

    // Unhook
    memcpy(pFunc, pOrgBytes, 5);

    // �޸� �Ӽ� ����
    VirtualProtect((LPVOID) pFunc, 5, dwOldProtect, &dwOldProtect);

    return TRUE;
}

BOOL hook_by_code(LPCTSTR szDllName, LPCTSTR szFuncName, PROC pfnNew, PBYTE pOrgBytes)
{
    FARPROC pfnOrg;
    DWORD dwOldProtect, dwAddress;
    BYTE pBuf[5] = { 0xE9, 0, };
    PBYTE pByte;

    // ��ŷ��� API �ּҸ� ���Ѵ�
    pfnOrg = (FARPROC) GetProcAddress(GetModuleHandle(szDllName), szFuncName);
    pByte = (PBYTE) pfnOrg;

    // ���� �̹� ��ŷ�Ǿ� �ִٸ� return FALSE
    if (pByte[0] == 0xE9)
        return FALSE;

    // 5 byte ��ġ�� ���Ͽ� �޸𸮿� WRITE �Ӽ� �߰�
    VirtualProtect((LPVOID) pfnOrg, 5, PAGE_EXECUTE_READWRITE, &dwOldProtect);

    // �����ڵ� (5 byte) ���
    memcpy(pOrgBytes, pfnOrg, 5);

    // JMP �ּҰ�� (E9 XXXX)
    // => XXXX = pfnNew - pfnOrg - 5
    dwAddress = (DWORD) pfnNew - (DWORD) pfnOrg - 5;
    memcpy(&pBuf[1], &dwAddress, 4);

    // Hook - 5 byte ��ġ(JMP XXXX)
    memcpy(pfnOrg, pBuf, 5);

    // �޸� �Ӽ� ����
    VirtualProtect((LPVOID) pfnOrg, 5, dwOldProtect, &dwOldProtect);

    return TRUE;
}


HANDLE hPipe;
BOOL bPipeConnected;

DWORD WINAPI PipeMan(LPVOID lParam)
{
    hPipe = CreateNamedPipe("\\\\.\\pipe\\osu!Lyrics", PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, BUF_SIZE * 5, 0, 1000, NULL);
    while (1)
    {
        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)
        {
            bPipeConnected = TRUE;
        }
        else
        {
            bPipeConnected = FALSE;
			break;
        }
        Sleep(1000);
    }
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}


typedef BOOL (WINAPI *tReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
tReadFile oReadFile;
BYTE bReadFile[5] = {};
CRITICAL_SECTION lReadFile;
OVERLAPPED overlapped;

DWORD dirLen = 4;
unordered_map<string, string> audioMap;

BOOL WINAPI hkReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    long long sReadFile = CurrentTime();
    EnterCriticalSection(&lReadFile);

    unhook_by_code("kernel32.dll", "ReadFile", bReadFile);
    BOOL rReadFile = oReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
    hook_by_code("kernel32.dll", "ReadFile", (PROC) hkReadFile, bReadFile);

    LeaveCriticalSection(&lReadFile);
    if (!rReadFile)
    {
        return FALSE;
    }

    char path[MAX_PATH];
    if (strnicmp(".osu", &path[GetFinalPathNameByHandle(hFile, path, MAX_PATH, VOLUME_NAME_DOS) - 4], 4) == 0 &&
        SetFilePointer(hFile, 0, NULL, FILE_CURRENT) == *lpNumberOfBytesRead)
    {
        char *buff = strdup((char *) lpBuffer);
        char *token = strtok(buff, "\n");
        while (token != NULL)
        {
            if (strnicmp(token, "AudioFilename:", 14) == 0)
            {
                char audio[MAX_PATH];

                int i = 14;
                for (; token[i] == ' '; i++);
                buff[0] = '\0';
                strncat(buff, &token[i], strlen(token) - i - 1);

                token = strdup(path);
                PathRemoveFileSpec(token);
                PathCombine(audio, token, buff);
                free(token);

                // �˻��� �� ��ҹ��� �����ϹǷ� ����� �� ���ϸ� ���
                WIN32_FIND_DATA fdata;
                FindClose(FindFirstFile(audio, &fdata));
                PathRemoveFileSpec(audio);
                PathCombine(audio, audio, fdata.cFileName);

                string tmp(audio);
                if (audioMap.find(tmp) == audioMap.end())
                {
                    audioMap.insert(make_pair(tmp, string(&path[dirLen])));
                }
                break;
            }
            token = strtok(NULL, "\n");
        }
        free(buff);
    }
    else if (bPipeConnected)
    {
        auto pair = audioMap.find(string(path)); // [ audioPath, beatmapPath ]
        if (pair != audioMap.end())
        {
            char buffer[BUF_SIZE];
			ZeroMemory(&buffer, sizeof(buffer));

			sprintf(buffer, "%llx|%s|%lx|%s\n", sReadFile, &path[dirLen], SetFilePointer(hFile, 0, NULL, FILE_CURRENT) - *lpNumberOfBytesRead, pair->second);

            if (!WriteFileEx(hPipe, buffer, strlen(buffer), &overlapped, [](DWORD, DWORD, LPOVERLAPPED) {}))
            {
                bPipeConnected = FALSE;
            }
        }
    }
    return TRUE;
}


HANDLE hPipeThread = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{

		hPipeThread = CreateThread(NULL, 0, PipeMan, NULL, 0, NULL);

		char dir[MAX_PATH];
		GetModuleFileName(NULL, dir, MAX_PATH);
		PathRemoveFileSpec(dir);
		dirLen = 4 + strlen(dir);


		char buffer[BUF_SIZE];
		ZeroMemory(&buffer, sizeof(buffer));

		sprintf(buffer, "%d", hinstDLL);
		while (bPipeConnected)
		{
			WriteFileEx(hPipe, buffer, strlen(buffer), &overlapped, [](DWORD, DWORD, LPOVERLAPPED) {});
			/*�̺κ� ������ ���� C#���� ���� �Ŀ� ���ڷ� �ٲ㼭 IntPtr�� ����ȭ���ּ���. ���� �۾��� �� ���Ŀ� ��ġ�մϴ�.*/
		}

        InitializeCriticalSection(&lReadFile);
        oReadFile = (tReadFile) GetProcAddress(GetModuleHandle("kernel32.dll"), "ReadFile");
        hook_by_code("kernel32.dll", "ReadFile", (PROC) hkReadFile, bReadFile);
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
		WaitForSingleObject(hPipeThread, INFINITE);
		CloseHandle(hPipeThread);

        EnterCriticalSection(&lReadFile);

        unhook_by_code("kernel32.dll", "ReadFile", bReadFile);

        LeaveCriticalSection(&lReadFile);
        DeleteCriticalSection(&lReadFile);
    }
    return TRUE;
}
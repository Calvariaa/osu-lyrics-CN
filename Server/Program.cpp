#define WIN32_LEAN_AND_MEAN
#pragma comment (lib, "Shlwapi.lib")

#include <Windows.h>
#include <Shlwapi.h>
#include <unordered_map>
#include <queue>
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
queue<string> pipeQueue;

DWORD WINAPI PipeManager(LPVOID lParam)
{
    hPipe = CreateNamedPipe("\\\\.\\pipe\\osu!Lyrics", PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, BUF_SIZE * 5, 0, INFINITE, NULL);
    // Ŭ���̾�Ʈ�� ������ ������ ���� ���
    while (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)
    {
        if (!pipeQueue.empty())
        {
            string message = pipeQueue.front();
            OVERLAPPED overlapped = {};
            if (WriteFileEx(hPipe, message.c_str(), message.length(), &overlapped, [](DWORD, DWORD, LPOVERLAPPED) {}))
            {
                pipeQueue.pop();
            }
        }
    }
    // Ŭ���̾�Ʈ ���� ����
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}


typedef BOOL (WINAPI *tReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
tReadFile pReadFile;
BYTE pReadFileJMP[5];
CRITICAL_SECTION hMutex;

unordered_map<string, string> audioInfo;

// osu!���� ReadFile�� ȣ���ϸ� ������ ������ osu!Lyrics�� ����
BOOL WINAPI hkReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    long long calledAt = CurrentTime();
    BOOL result = FALSE;

    EnterCriticalSection(&hMutex);
    {
        unhook_by_code("kernel32.dll", "ReadFile", pReadFileJMP);
        result = pReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
        hook_by_code("kernel32.dll", "ReadFile", (PROC) hkReadFile, pReadFileJMP);
    }
    LeaveCriticalSection(&hMutex);
    if (!result)
    {
        // ��� ReadFile�� �����ϴ� ���� osu �������� ���� ��...
        return FALSE;
    }

    char path[MAX_PATH];
    DWORD pathLength = GetFinalPathNameByHandle(hFile, path, MAX_PATH, VOLUME_NAME_DOS);
    //                  1: \\?\D:\Games\osu!\...
    DWORD seekPosition = SetFilePointer(hFile, 0, NULL, FILE_CURRENT) - *lpNumberOfBytesRead;
    // ���� �д� ������ ��Ʈ�� �����̰� �պκ��� �о��ٸ�:
    // AudioFilename�� �պκп� ���� / ���� �ڵ� �� ���� ���� �� �� ���� ����!
    if (strnicmp(".osu", &path[pathLength - 4], 4) == 0 && seekPosition == 0)
    {
        // strtok�� �ҽ��� �����ϹǷ� �ϴ� ���
        char *buffer = strdup((char *) lpBuffer);
        char *line = strtok(buffer, "\n");
        while (line != NULL)
        {
            // ��Ʈ���� ���� ���� ��� ���
            if (strnicmp(line, "AudioFilename:", 14) == 0)
            {
                char *beatmapDir = strdup(path);
                PathRemoveFileSpec(beatmapDir);

                char audioPath[MAX_PATH];

                // get value & trim
                int i = 14;
                for (; line[i] == ' '; i++);
                buffer[0] = '\0';
                strncat(buffer, &line[i], strlen(line) - i - 1);
                PathCombine(audioPath, beatmapDir, buffer);

                // �˻��� �� ��ҹ��� �����ϹǷ� ����� �� ���� ��� ���
                WIN32_FIND_DATA fdata;
                FindClose(FindFirstFile(audioPath, &fdata));
                PathRemoveFileSpec(audioPath);
                PathCombine(audioPath, audioPath, fdata.cFileName);

                audioInfo.insert(make_pair(string(audioPath), string(path)));
                
                free(beatmapDir);
                break;
            }
            line = strtok(NULL, "\n");
        }
        free(buffer);
    }
    else
    {
        // [ audioPath, beatmapPath ]
        unordered_map<string, string>::iterator it = audioInfo.find(string(path));
        if (it != audioInfo.end())
        {
            char buffer[BUF_SIZE];
            sprintf(buffer, "%llx|%s|%lx|%s\n", calledAt, &path[4], seekPosition, &it->second[4]);
            pipeQueue.push(string(buffer));
        }
    }
    return TRUE;
}


HANDLE hPipeThread;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        hPipeThread = CreateThread(NULL, 0, PipeManager, NULL, 0, NULL);

        char buffer[BUF_SIZE];
        sprintf(buffer, "_%d\n", hinstDLL);
        pipeQueue.push(string(buffer));
        
        InitializeCriticalSection(&hMutex);
        EnterCriticalSection(&hMutex);
        {
            pReadFile = (tReadFile) GetProcAddress(GetModuleHandle("kernel32.dll"), "ReadFile");
            hook_by_code("kernel32.dll", "ReadFile", (PROC) hkReadFile, pReadFileJMP);
        }
        LeaveCriticalSection(&hMutex);
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        DisconnectNamedPipe(hPipe);
        WaitForSingleObject(hPipeThread, INFINITE);
        CloseHandle(hPipeThread);

        EnterCriticalSection(&hMutex);
        {
            unhook_by_code("kernel32.dll", "ReadFile", pReadFileJMP);
        }
        LeaveCriticalSection(&hMutex);
        DeleteCriticalSection(&hMutex);
    }
    return TRUE;
}
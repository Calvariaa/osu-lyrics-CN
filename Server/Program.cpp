#define WIN32_LEAN_AND_MEAN
#pragma comment (lib, "Shlwapi.lib")

#include <Windows.h>
#include <Shlwapi.h>
#include <unordered_map>
#include <mutex>
#include "ConcurrentQueue.h"
#include "Hooker.h"
using namespace std;

#define BUF_SIZE MAX_PATH * 3

long long CurrentTime()
{
    long long t;
    GetSystemTimeAsFileTime((LPFILETIME) &t);
    return t;
}


HANDLE hPipe;
volatile bool bCancelPipeThread;
volatile bool bPipeConnected;

ConcurrentQueue<string> MessageQueue;

DWORD WINAPI PipeThread(LPVOID lParam)
{
    hPipe = CreateNamedPipe("\\\\.\\pipe\\osu!Lyrics", PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, BUF_SIZE, 0, INFINITE, NULL);
    // ������ ���� ��û�� ���� ������ Ŭ���̾�Ʈ ���� ���� ���
    while (!bCancelPipeThread)
    {
        // ConnectNamedPipe�� Ŭ���̾�Ʈ�� ����� ������ ���� �����:
        // ��Ҵ� DisconnectNamedPipe�� ����
        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)
        {
            bPipeConnected = true;

            if (MessageQueue.Empty())
            {
                // �޼��� ť�� ����� �� 3�ʰ� ��ٷ��� ��ȣ�� ������ �ٽ� ��ٸ�
                MessageQueue.WaitPush(3000);
                continue;
            }

            DWORD wrote;
            string message = MessageQueue.Pop();
            if (WriteFile(hPipe, message.c_str(), message.length(), &wrote, NULL))
            {
                continue;
            }
        }
        bPipeConnected = false;
        DisconnectNamedPipe(hPipe);

        MessageQueue.Clear();
    }
    // Ŭ���̾�Ʈ ���� ����
    bPipeConnected = false;
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}


Hooker<tReadFile> hkrReadFile("kernel32.dll", "ReadFile");
unordered_map<string, string> AudioInfo;

// osu!���� ReadFile�� ȣ���ϸ� ������ ������ osu!Lyrics�� ����
BOOL WINAPI hkReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    long long calledAt = CurrentTime();

    hkrReadFile.EnterCS();
    hkrReadFile.Unhook();
    BOOL result = hkrReadFile.pFunction(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
    hkrReadFile.Hook();
    hkrReadFile.LeaveCS();
    if (!result)
    {
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

                AudioInfo.insert(make_pair(string(audioPath), string(path)));

                free(beatmapDir);
                break;
            }
            line = strtok(NULL, "\n");
        }
        free(buffer);
    }
    // ������ ������ �������� ������ ���縦 ���� ���� �ʴٴ� ��:
    // osu!�� ���� �÷��̿��� �����ϰ� ����... �ڿ� ���� ����
    else if (bPipeConnected)
    {
        // [ audioPath, beatmapPath ]
        unordered_map<string, string>::iterator pair = AudioInfo.find(string(path));
        if (pair != AudioInfo.end())
        {
            char message[BUF_SIZE];
            sprintf(message, "%llx|%s|%lx|%s\n", calledAt, &path[4], seekPosition, &pair->second[4]);
            MessageQueue.Push(string(message));
        }
    }
    return TRUE;
}


HANDLE hPipeThread;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        hPipeThread = CreateThread(NULL, 0, PipeThread, NULL, 0, NULL);

        hkrReadFile.Set(hkReadFile);
        hkrReadFile.Hook();
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        // hkrReadFile.EnterCS();
        hkrReadFile.Unhook();
        // hkrReadFile.LeaveCS();

        bCancelPipeThread = true;
        DisconnectNamedPipe(hPipe);
        WaitForSingleObject(hPipeThread, INFINITE);
        CloseHandle(hPipeThread);
    }
    return TRUE;
}

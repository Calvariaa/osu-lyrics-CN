#define WIN32_LEAN_AND_MEAN
#pragma comment (lib, "Shlwapi.lib")

#include <Windows.h>
#include <Shlwapi.h>
#include <unordered_map>
#include <cstring>
#include <string>
#include <utility>
#include "bass.h"
#include "bass_fx.h"
#include "ConcurrentQueue.h"
#include "Hooker.h"
using namespace std;

#define BUF_SIZE MAX_PATH * 3

inline long long CurrentTime()
{
    long long t;
    GetSystemTimeAsFileTime((LPFILETIME) &t);
    return t;
}


ConcurrentQueue<string> MessageQueue;

HANDLE hPipeThread;
HANDLE hPipe;
volatile bool bCancelPipeThread;
volatile bool bPipeConnected;
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


pair<string, string> Playing;
unordered_map<string, string> AudioInfo;

Hooker<tReadFile> hkrReadFile("kernel32.dll", "ReadFile", hkReadFile);
BOOL WINAPI hkReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    BOOL result;

    hkrReadFile.EnterCS();
    hkrReadFile.Unhook();
    result = hkrReadFile.pFunction(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
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
        for (char *line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n"))
        {
            // ��Ʈ���� ���� ���� ��� ���
            if (strnicmp(line, "AudioFilename:", 14) != 0)
            {
                continue;
            }

            char *beatmapDir = strdup(path);
            PathRemoveFileSpec(beatmapDir);

            char audioPath[MAX_PATH];

            // get value & trim
            int i = 14;
            for (; line[i] == ' '; i++);
            buffer[0] = NULL;
            strncat(buffer, &line[i], strlen(line) - i - 1);
            PathCombine(audioPath, beatmapDir, buffer);

            // �˻��� �� ��ҹ��� �����ϹǷ� ����� �� ���� ��� ���
            WIN32_FIND_DATA fdata;
            FindClose(FindFirstFile(audioPath, &fdata));
            PathRemoveFileSpec(audioPath);
            PathCombine(audioPath, audioPath, fdata.cFileName);

            AudioInfo.emplace(audioPath, path);

            free(beatmapDir);
            break;
        }
        free(buffer);
    }
    else
    {
        // [ audioPath, beatmapPath ]
        unordered_map<string, string>::iterator pair = AudioInfo.find(path);
        if (pair != AudioInfo.end())
        {
            Playing = { pair->first.substr(4), pair->second.substr(4) };
        }
    }

    return TRUE;
}


Hooker<tBASS_ChannelPlay> hkrPlay("bass.dll", "BASS_ChannelPlay", hkBASS_ChannelPlay);
BOOL BASSDEF(hkBASS_ChannelPlay)(DWORD handle, BOOL restart)
{
    BOOL result;

    hkrPlay.EnterCS();
    hkrPlay.Unhook();
    result = hkrPlay.pFunction(handle, restart);
    hkrPlay.Hook();
    hkrPlay.LeaveCS();
    if (!result)
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        float tempo; BASS_ChannelGetAttribute(handle, BASS_ATTRIB_TEMPO, &tempo);
        hkBASS_Control(currentTime, tempo);
    }
    return TRUE;
}

Hooker<tBASS_ChannelSetPosition> hkrSetPos("bass.dll", "BASS_ChannelSetPosition", hkBASS_ChannelSetPosition);
BOOL BASSDEF(hkBASS_ChannelSetPosition)(DWORD handle, QWORD pos, DWORD mode)
{
    BOOL result;

    hkrSetPos.EnterCS();
    hkrSetPos.Unhook();
    result = hkrSetPos.pFunction(handle, pos, mode);
    hkrSetPos.Hook();
    hkrSetPos.LeaveCS();
    if (!result)
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, pos);
        float tempo; BASS_ChannelGetAttribute(handle, BASS_ATTRIB_TEMPO, &tempo);
        // ����!! pos�� ���� ������ ��,
        // ����ϸ� BASS_ChannelPlay��� �� �Լ��� ȣ��ǰ�,
        // BASS_ChannelIsActive ���� BASS_ACTIVE_PAUSED��.
        if (BASS_ChannelIsActive(handle) == BASS_ACTIVE_PAUSED)
        {
            tempo = -100;
        }
        hkBASS_Control(currentTime, tempo);
    }
    return TRUE;
}

Hooker<tBASS_ChannelSetAttribute> hkrSetAttr("bass.dll", "BASS_ChannelSetAttribute", hkBASS_ChannelSetAttribute);
BOOL BASSDEF(hkBASS_ChannelSetAttribute)(DWORD handle, DWORD attrib, float value)
{
    BOOL result;

    hkrSetAttr.EnterCS();
    hkrSetAttr.Unhook();
    result = hkrSetAttr.pFunction(handle, attrib, value);
    hkrSetAttr.Hook();
    hkrSetAttr.LeaveCS();
    if (!result)
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if ((info.ctype & BASS_CTYPE_STREAM) && attrib == BASS_ATTRIB_TEMPO)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        hkBASS_Control(currentTime, value);
    }
    return TRUE;
}

Hooker<tBASS_ChannelPause> hkrPause("bass.dll", "BASS_ChannelPause", hkBASS_ChannelPause);
BOOL BASSDEF(hkBASS_ChannelPause)(DWORD handle)
{
    BOOL result;

    hkrPause.EnterCS();
    hkrPause.Unhook();
    result = hkrPause.pFunction(handle);
    hkrPause.Hook();
    hkrPause.LeaveCS();
    if (!result)
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        hkBASS_Control(currentTime, -100);
    }
    return TRUE;
}

void hkBASS_Control(double currentTime, float tempo)
{
    if (!bPipeConnected)
    {
        return;
    }

    char message[BUF_SIZE];
    sprintf(message, "%llx|%s|%lf|%f|%s\n", CurrentTime(), Playing.first.c_str(), currentTime, tempo, Playing.second.c_str());
    MessageQueue.Push(message);
}


BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        hPipeThread = CreateThread(NULL, 0, PipeThread, NULL, 0, NULL);

        hkrPlay.Hook();
        hkrSetAttr.Hook();
        hkrSetPos.Hook();
        hkrPause.Hook();

        hkrReadFile.Hook();
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        hkrReadFile.EnterCS();
        hkrReadFile.Unhook();
        hkrReadFile.LeaveCS();

        hkrPause.EnterCS();
        hkrPause.Unhook();
        hkrPause.LeaveCS();
        hkrSetAttr.EnterCS();
        hkrSetAttr.Unhook();
        hkrSetAttr.LeaveCS();
        hkrSetPos.EnterCS();
        hkrSetPos.Unhook();
        hkrSetPos.LeaveCS();
        hkrPlay.EnterCS();
        hkrPlay.Unhook();
        hkrPlay.LeaveCS();

        bCancelPipeThread = true;
        DisconnectNamedPipe(hPipe);
        WaitForSingleObject(hPipeThread, INFINITE);
        CloseHandle(hPipeThread);
    }
    return TRUE;
}

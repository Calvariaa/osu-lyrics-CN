#pragma comment(lib, "Shlwapi.lib")

#include "Observer.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <utility>
#include <functional>
#include <concurrent_unordered_map.h>

#include <Windows.h>
#include <Shlwapi.h>
#include "bass.h"
#include "bass_fx.h"
#include "Hooker.h"
#include "Server.h"

std::shared_ptr<Observer> Observer::instance;
std::once_flag Observer::once_flag;

BOOL WINAPI Observer::ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    Observer *self = Observer::GetInstance();
    if (!self->hookerReadFile.GetFunction()(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped))
    {
        return FALSE;
    }

    // osu!�� �� ���� �̸�(260�� �̻�)�� �������� �����Ƿ�,
    // szFilePath ���̴� MAX_PATH�� �ϰ� �߰� �Ҵ��ϴ� ������ ����
    // GetFinalPathNameByHandle �����, szFilePath ���� \\?\D:\Games\osu!\...
    wchar_t szFilePath[MAX_PATH];
    DWORD nFilePathLength = GetFinalPathNameByHandle(hFile, szFilePath, MAX_PATH, VOLUME_NAME_DOS);
    // �д� �� ��ũ ������ �ƴϰų�, osu!�� �ɷ��� �ʰ��� ��Ÿ library�� �۾�?
    if (nFilePathLength == 0 || nFilePathLength > MAX_PATH || lpNumberOfBytesRead == NULL)
    {
        return TRUE;
    }

    DWORD dwFilePosition = SetFilePointer(hFile, 0, NULL, FILE_CURRENT) - *lpNumberOfBytesRead;
    // ���� �д� ������ ��Ʈ�� �����̰� �պκ��� �о��ٸ� ���� ���� ��� ���:
    // AudioFilename�� �պκп� ���� / ���� �ڵ� �� ���� ���� �� �� ���� ����!
    if (wcsnicmp(L".osu", &szFilePath[nFilePathLength - 4], 4) == 0 && dwFilePosition == 0)
    {
        // strtok�� �ҽ��� �����ϹǷ� �ϴ� ���
        // .osu ������ UTF-8(Multibyte) ���ڵ�
        char *buffer = strdup((const char *) lpBuffer);
        for (char *line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n"))
        {
            if (strnicmp(line, "AudioFilename:", 14) != 0)
            {
                continue;
            }

            // AudioFilename �� ���
            wchar_t szAudioFileName[MAX_PATH];
            mbstowcs(szAudioFileName, &line[14], MAX_PATH);
            StrTrim(szAudioFileName, L" \r");

            // ��Ʈ�� ������ �������� ���� ������ ��� ã��
            wchar_t szAudioFilePath[MAX_PATH];
            wcscpy(szAudioFilePath, szFilePath);
            PathRemoveFileSpec(szAudioFilePath);
            PathCombine(szAudioFilePath, szAudioFilePath, szAudioFileName);

            // audioInfo���� ���� ������ �˻��� �� ��ҹ��� �����ϹǷ� ��Ȯ�� ���� ��� ���
            WIN32_FIND_DATA fdata;
            FindClose(FindFirstFile(szAudioFilePath, &fdata));
            PathRemoveFileSpec(szAudioFilePath);
            PathCombine(szAudioFilePath, szAudioFilePath, fdata.cFileName);

            // PathCombineW�� \\?\(Long Unicode path prefix)�� �����ϴµ�,
            // GetFinalPathNameByHandle ���忡 �°� �ٽ� �߰��ؼ�
            // ������ �ٲ��� ������ �� �� ȥ�������� ����
            wcscpy(szAudioFileName, szAudioFilePath);
            wcscpy(szAudioFilePath, L"\\\\?\\");
            wcscat(szAudioFilePath, szAudioFileName);

            // osu!���� ��Ʈ���� �ٲ� �� �Ź� ��Ʈ�� ������ ���� �ʰ� ĳ�ÿ��� �ҷ����⵵ ��
            // => ��Ʈ�� ���Ϻ��ٴ� ���� ������ ���� �� ��� ���� �����ؾ�
            self->audioInfo.insert({szAudioFilePath, szFilePath});
            break;
        }
        free(buffer);
    }
    // ���� �д� ������ ��Ʈ�� ���� ������ �� ��� ���� �����ϱ�
    else
    {
        decltype(self->audioInfo)::iterator info;
        if ((info = self->audioInfo.find(szFilePath)) != self->audioInfo.end())
        {
            EnterCriticalSection(&self->hCritiaclSection);
            self->playing = {info->first.substr(4), info->second.substr(4)};
            LeaveCriticalSection(&self->hCritiaclSection);
        }
    }
    return TRUE;
}


inline long long GetSystemTimeAsFileTime()
{
    /*
    * Do not cast a pointer to a FILETIME structure to either a
    * ULARGE_INTEGER* or __int64* value because it can cause alignment faults on 64-bit Windows.
    * via  http://technet.microsoft.com/en-us/library/ms724284(v=vs.85).aspx
    */
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((long long) ft.dwHighDateTime << 32) + ft.dwLowDateTime;
}

BOOL WINAPI Observer::BASS_ChannelPlay(DWORD handle, BOOL restart)
{
    Observer *self = Observer::GetInstance();
    if (!self->hookerBASS_ChannelPlay.GetFunction()(handle, restart))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        float tempo; BASS_ChannelGetAttribute(handle, BASS_ATTRIB_TEMPO, &tempo);
        self->Update(currentTime, tempo);
    }
    return TRUE;
}

BOOL WINAPI Observer::BASS_ChannelSetPosition(DWORD handle, QWORD pos, DWORD mode)
{
    Observer *self = Observer::GetInstance();
    if (!self->hookerBASS_ChannelSetPosition.GetFunction()(handle, pos, mode))
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
        self->Update(currentTime, tempo);
    }
    return TRUE;
}

BOOL WINAPI Observer::BASS_ChannelSetAttribute(DWORD handle, DWORD attrib, float value)
{
    Observer *self = Observer::GetInstance();
    if (!self->hookerBASS_ChannelSetAttribute.GetFunction()(handle, attrib, value))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if ((info.ctype & BASS_CTYPE_STREAM) && attrib == BASS_ATTRIB_TEMPO)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        self->Update(currentTime, value);
    }
    return TRUE;
}

BOOL WINAPI Observer::BASS_ChannelPause(DWORD handle)
{
    Observer *self = Observer::GetInstance();
    if (!self->hookerBASS_ChannelPause.GetFunction()(handle))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        self->Update(currentTime, -100);
    }
    return TRUE;
}

void Observer::Update(double currentTime, float tempo)
{
    Observer *self = Observer::GetInstance();
    wchar_t message[Server::nMessageLength];
    EnterCriticalSection(&self->hCritiaclSection);
    swprintf(message, L"%llx|%s|%lf|%f|%s\n",
        GetSystemTimeAsFileTime(),
        self->playing.first.c_str(),
        currentTime,
        tempo,
        self->playing.second.c_str()
    );
    LeaveCriticalSection(&self->hCritiaclSection);
    Server::GetInstance()->PushMessage(message);
}

void Observer::Run()
{
    this->hookerReadFile.Hook();

    this->hookerBASS_ChannelPlay.Hook();
    this->hookerBASS_ChannelSetPosition.Hook();
    this->hookerBASS_ChannelSetAttribute.Hook();
    this->hookerBASS_ChannelPause.Hook();
}

void Observer::Stop()
{
    this->hookerBASS_ChannelPause.Unhook();
    this->hookerBASS_ChannelSetAttribute.Unhook();
    this->hookerBASS_ChannelSetPosition.Unhook();
    this->hookerBASS_ChannelPlay.Unhook();

    this->hookerReadFile.Unhook();
}

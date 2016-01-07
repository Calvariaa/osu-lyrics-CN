#pragma comment(lib, "Shlwapi.lib")

#include "Observer.h"

#include <cstdio>
#include <cstdlib>
#include <tchar.h>
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

    TCHAR szFilePath[MAX_PATH];
    DWORD nFilePathLength = GetFinalPathNameByHandle(hFile, szFilePath, MAX_PATH, VOLUME_NAME_DOS);
    //                  1: \\?\D:\Games\osu!\...
    DWORD dwFilePosition = SetFilePointer(hFile, 0, NULL, FILE_CURRENT) - *lpNumberOfBytesRead;
    // ���� �д� ������ ��Ʈ�� �����̰� �պκ��� �о��ٸ� ���� ���� ��� ���:
    // AudioFilename�� �պκп� ���� / ���� �ڵ� �� ���� ���� �� �� ���� ����!
    if (_tcsnicmp(_T(".osu"), &szFilePath[nFilePathLength - 4], 4) == 0 && dwFilePosition == 0)
    {
        // strtok�� �ҽ��� �����ϹǷ� �ϴ� ���
        // .osu ������ UTF-8(Multibyte) ���ڵ�
        char *buffer = strdup(reinterpret_cast<char *>(lpBuffer));
        for (char *line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n"))
        {
            if (strnicmp(line, "AudioFilename:", 14) != 0)
            {
                continue;
            }

            // AudioFilename �� ���
            TCHAR szAudioFileName[MAX_PATH];
#ifdef UNICODE
            mbstowcs(szAudioFileName, &line[14], MAX_PATH);
#else
            strncpy(szAudioFileName, &line[14], MAX_PATH);
#endif
            StrTrim(szAudioFileName, _T(" \r"));

            TCHAR szAudioFilePath[MAX_PATH];
            _tcscpy(szAudioFilePath, szFilePath);
            PathRemoveFileSpec(szAudioFilePath);
            PathCombine(szAudioFilePath, szAudioFilePath, szAudioFileName);

            // �˻��� �� ��ҹ��� �����ϹǷ� ��Ȯ�� ���� ��� ���
            WIN32_FIND_DATA fdata;
            FindClose(FindFirstFile(szAudioFilePath, &fdata));
            PathRemoveFileSpec(szAudioFilePath);
            PathCombine(szAudioFilePath, szAudioFilePath, fdata.cFileName);
#ifdef UNICODE
            // PathCombineW�� \\?\�� �����Ѵ�;;
            TCHAR *szAudioFilePathStripped = _tcsdup(szAudioFilePath);
            _tcscpy(szAudioFilePath, _T("\\\\?\\"));
            _tcscat(szAudioFilePath, szAudioFilePathStripped);
            free(szAudioFilePathStripped);
#endif

            self->audioInfo.insert({ szAudioFilePath, szFilePath });
            break;
        }
        free(buffer);
    }
    else
    {
        // [ audioPath, beatmapPath ]
        decltype(self->audioInfo)::iterator info;
        if ((info = self->audioInfo.find(szFilePath)) != self->audioInfo.end())
        {
            EnterCriticalSection(&self->hCritiaclSection);
            self->playing = { info->first.substr(4), info->second.substr(4) };
            LeaveCriticalSection(&self->hCritiaclSection);
        }
    }
    return TRUE;
}


inline long long Now()
{
    long long t;
    GetSystemTimeAsFileTime(reinterpret_cast<LPFILETIME>(&t));
    return t;
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
        self->Report_BASS(Now(), currentTime, tempo);
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
        self->Report_BASS(Now(), currentTime, tempo);
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
        self->Report_BASS(Now(), currentTime, value);
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
        self->Report_BASS(Now(), currentTime, -100);
    }
    return TRUE;
}

void Observer::Report_BASS(long long calledAt, double currentTime, float tempo)
{
    TCHAR message[Server::nMessageLength];
    tstring audioPath, beatmapPath;
    Observer *self = Observer::GetInstance();
    EnterCriticalSection(&self->hCritiaclSection);
    std::tie(audioPath, beatmapPath) = self->playing;
    LeaveCriticalSection(&self->hCritiaclSection);
    _stprintf(message, _T("%llx|%s|%lf|%f|%s\n"), calledAt, audioPath.c_str(), currentTime, tempo, beatmapPath.c_str());
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

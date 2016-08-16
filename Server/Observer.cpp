#pragma comment(lib, "Shlwapi.lib")

#pragma warning(disable:4996)

#include "Observer.h"

#include <cstdio>

#include <Windows.h>
#include <Shlwapi.h>
#include "bass.h"
#include "bass_fx.h"
#include "Hooker.h"
#include "Server.h"

#define AUDIO_FILE_INFO_TOKEN "AudioFilename:"
#define AUDIO_FILE_INFO_TOKEN_LENGTH 14

inline wchar_t* CutStringFromFront(wchar_t* string, unsigned int index)
{
    return (string + index);
}

BOOL WINAPI proxyReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    if (!InstanceObserver.hookerReadFile.GetOriginalFunction()(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped))
    {
        return FALSE;
    }

    TCHAR nameFilePath[MAX_PATH];
    DWORD dwFilePathLength = GetFinalPathNameByHandle(hFile, nameFilePath, MAX_PATH, VOLUME_NAME_DOS);
    DWORD dwFilePosition = SetFilePointer(hFile, 0, NULL, FILE_CURRENT) - (*lpNumberOfBytesRead);

    // ���� �д� ������ ��Ʈ�� �����̰� �պκ��� �о��ٸ� ���� ���� ��� ���:
    // ���� �̸��� ���Ե� Path ���κ� 4���ڸ� �ڸ�. 4���ڿ�  .osu�� ���Ͽ� �� ������ osu �������� Ȯ����
    if (wcsncmp(L".osu", &nameFilePath[dwFilePathLength - 4], 4) == 0 && dwFilePosition == 0)
    {
        // .osu ������ UTF-8(Multibyte) ���ڵ�
        /* strtok�� �ҽ��� �����ϹǷ� ���� strdup�� �̿��ؼ� ���ڸ� ������. */
        LPSTR buffer = strdup((const char*)(lpBuffer));

        /* for���� �̿��ؼ� �ٸ��� strtok���� �߶�. */
		for (LPSTR lineFile = strtok(buffer, "\n"); lineFile != NULL; lineFile = strtok(NULL, "\n"))
        {
            /* �߶� �ٿ� Token�� �ִ��� Ȯ���ϰ� �ƴҰ�쿡�� continue��. */
            if (strnicmp(lineFile, AUDIO_FILE_INFO_TOKEN, AUDIO_FILE_INFO_TOKEN_LENGTH) != 0)
            {
                continue;
            }

            /* NOTE: ����� ������ؼ� ���� ���Ҷ����� "NOT TO FIX" ���� ����� �Ҷ����� ������ ���� ���ɴϴ�.     */
            /* �׸��� Path �Լ����� �̿��ؼ� ���ڿ������� //?/�� �����Ϸ��� ���� ������. �Լ��� �ǹ̸� �𸣰Ե˴ϴ�. */ 
            /* //?/ �� 4����. �� ���� 4���ں��� �����͸� �����ϸ� �ڿ������� //?/�� ������ �ֽ��ϴ�.                 */

            TCHAR nameAudioFile[MAX_PATH];

            mbstowcs(nameAudioFile, &lineFile[AUDIO_FILE_INFO_TOKEN_LENGTH], MAX_PATH);
            StrTrimW(nameAudioFile, L" \r");

            TCHAR nameAudioFilePath[MAX_PATH];

			/* �պκ��� "//?/" �� �����ϱ����� ���� 4��° ���ں��� ����. (&nameFilePath[4]) */
            wcscpy(nameAudioFilePath, CutStringFromFront(nameFilePath, 4));
            /* nameAudioFilePath���� ���ϸ��� ����ϴ�. */
            PathRemoveFileSpecW(nameAudioFilePath);
            /* ���ϸ��� ������ Path�� nameAudioFilePath�� nameAudioFile�ٿ�, ������ Path�� ����ϴ�. */
            PathCombineW(nameAudioFilePath, nameAudioFilePath, nameAudioFile);

			EnterCriticalSection(&InstanceObserver.hCritiaclSection);

            /* �պκ��� "//?/" �� �����ϱ����� ���� 4��° ���ں��� ����. (&nameFilePath[4]) */
            InstanceObserver.currentPlaying.beatmapPath = (std::wstring(CutStringFromFront(nameFilePath, 4)));
			InstanceObserver.currentPlaying.audioPath = (std::wstring(nameAudioFilePath));

			LeaveCriticalSection(&InstanceObserver.hCritiaclSection);

            break;
        }

        /* strdup�� �̿��� ������ ���ڿ��� �޸𸮸� ������ŵ�ϴ�. */
        free(buffer);
    }
    return TRUE;
}

/* ���� �ý��� �ð��� ���մϴ�. */
inline long long GetCurrentSysTime()
{
    long long time;

    GetSystemTimeAsFileTime((LPFILETIME)&time); return time;
}

BOOL WINAPI proxyBASS_ChannelPlay(DWORD handle, BOOL restart)
{
    if (!InstanceObserver.hookerBASS_ChannelPlay.GetOriginalFunction()(handle, restart))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);

    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTimePos = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        float tempo; BASS_ChannelGetAttribute(handle, BASS_ATTRIB_TEMPO, &tempo);
        InstanceObserver.SendInfomation(GetCurrentSysTime(), currentTimePos, tempo);
    }
    return TRUE;
}

BOOL WINAPI proxyBASS_ChannelSetPosition(DWORD handle, QWORD pos, DWORD mode)
{
    if (!InstanceObserver.hookerBASS_ChannelSetPosition.GetOriginalFunction()(handle, pos, mode))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);

    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, pos);
        float CurrentTempo = 0; 
        BASS_ChannelGetAttribute(handle, BASS_ATTRIB_TEMPO, &CurrentTempo);
        // ����!! pos�� ���� ������ ��,
        // ����ϸ� proxyBASS_ChannelPlay��� �� �Լ��� ȣ��ǰ�,
        // BASS_ChannelIsActive ���� BASS_ACTIVE_PAUSED��.
        if (BASS_ChannelIsActive(handle) == BASS_ACTIVE_PAUSED)
        {
            CurrentTempo = -100;
        }

        InstanceObserver.SendInfomation(GetCurrentSysTime(), currentTime, CurrentTempo);
    }
    return TRUE;
}

BOOL WINAPI proxyBASS_ChannelSetAttribute(DWORD handle, DWORD attrib, float value)
{
    if (!InstanceObserver.hookerBASS_ChannelSetAttribute.GetOriginalFunction()(handle, attrib, value))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);

    if ((info.ctype & BASS_CTYPE_STREAM) && attrib == BASS_ATTRIB_TEMPO)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        InstanceObserver.SendInfomation(GetCurrentSysTime(), currentTime, value);
    }
    return TRUE;
}

BOOL WINAPI proxyBASS_ChannelPause(DWORD handle)
{
    if (!InstanceObserver.hookerBASS_ChannelPause.GetOriginalFunction()(handle))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        InstanceObserver.SendInfomation(GetCurrentSysTime(), currentTime, -100);
    }
    return TRUE;
}

void Observer::SendInfomation(long long calledAt, double currentTime, float tempo)
{
    TCHAR message[Server::MAX_MESSAGE_LENGTH];

	/* ���� ������ �÷����ϰ��ִ��� proxyReadFile ���� �� ���� Ŭ���̾�Ʈ�� �����մϴ�. */
    EnterCriticalSection(&InstanceObserver.hCritiaclSection);
    swprintf(message, L"%llx|%s|%lf|%f|%s\n", 
		calledAt, 
		InstanceObserver.currentPlaying.audioPath.c_str(),
		currentTime, 
		tempo, 
		InstanceObserver.currentPlaying.beatmapPath.c_str());
	LeaveCriticalSection(&InstanceObserver.hCritiaclSection);

    InstanceServer.PushMessage(message);
}

void Observer::Start()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

    this->hookerReadFile.Hook();
    this->hookerBASS_ChannelPlay.Hook();
    this->hookerBASS_ChannelSetPosition.Hook();
    this->hookerBASS_ChannelSetAttribute.Hook();
    this->hookerBASS_ChannelPause.Hook();

	DetourTransactionCommit();
}

void Observer::Stop()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

    this->hookerBASS_ChannelPause.Unhook();
    this->hookerBASS_ChannelSetAttribute.Unhook();
    this->hookerBASS_ChannelSetPosition.Unhook();
    this->hookerBASS_ChannelPlay.Unhook();
    this->hookerReadFile.Unhook();

	DetourTransactionCommit();
}

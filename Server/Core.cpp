#include "Core.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <map>
#include <cstdio>
#include <mutex>

#pragma comment(lib, "Shlwapi.lib")

std::wstring                            g_audioPath;
std::wstring                            g_beatmapPath;

std::map<std::wstring, std::wstring>    g_songCached;
std::mutex                              g_songMutex;

NamedPipe                               g_namedPipe
;

//
// Proxy ���Ǻ�. (#define ���� ���ǵǾ�����.)
//      MAKE_PROXY_DEF(module, function, proxy)
//      HookEngine<decltype(proxy)> proxy__hk = HookEngine<decltype(proxy)>(module, function, proxy)
// EXAMPLE : 
//      MAKE_PROXY_DEF(NAME_KERNEL_DLL, "ReadFile", proxyReadFile);
//      ==> HookEngine< decltype(proxyReadFile) > proxyReadFile__hk = HookEngine< decltype(proxy) >(module, function, proxy);
//

MAKE_PROXY_DEF(NAME_KERNEL_DLL, "ReadFile",                 proxyReadFile);
MAKE_PROXY_DEF(NAME_BASS_DLL,   "BASS_ChannelPlay",         proxyBASS_ChannelPlay);
MAKE_PROXY_DEF(NAME_BASS_DLL,   "BASS_ChannelSetPosition",  proxyBASS_ChannelSetPosition);
MAKE_PROXY_DEF(NAME_BASS_DLL,   "BASS_ChannelSetAttribute", proxyBASS_ChannelSetAttribute);
MAKE_PROXY_DEF(NAME_BASS_DLL,   "BASS_ChannelPause",        proxyBASS_ChannelPause);

void Start()
{
    BeginHook();                                // Begin Hooking Method.
    EngineHook(proxyReadFile);                      // ReadFile Hook.
    EngineHook(proxyBASS_ChannelPlay);              // ChannelPlay Hook.
    EngineHook(proxyBASS_ChannelSetPosition);       // ChannelSetPosition Hook.
    EngineHook(proxyBASS_ChannelSetAttribute);      // ChannelSetAttribute Hook.
    EngineHook(proxyBASS_ChannelPause);             // ChannelPause Hook.
    EndHook();                                  // End Hooking Method.

    g_namedPipe.Start(NAME_NAMED_PIPE);
}

void Stop()
{
    BeginHook();                                // Begin Hooking Method.
    EngineUnhook(proxyReadFile);                    // ReadFile Unhook.
    EngineUnhook(proxyBASS_ChannelPlay);            // ChannelPlay Unhook.
    EngineUnhook(proxyBASS_ChannelSetPosition);     // ChannelSetPostion Unhook.
    EngineUnhook(proxyBASS_ChannelSetAttribute);    // ChannelSetAttribute Unhook.
    EngineUnhook(proxyBASS_ChannelPause);           // ChannelPause Unhook.
    EndHook();                                  // End Hooking Method.
    
    g_namedPipe.Stop();
}

//
// TODO : currentTime�� tempo�� ������ �ǹ��ϴ��� �˼� �����ϴ�.
//        �ּ����� ���� ��Ź�帳�ϴ�.
//
void Notify(double currentTime, float tempo)
{
    wchar_t message[MAX_MESSAGE_LENGTH];

    long long sysTime;
    GetSystemTime((LPSYSTEMTIME)&sysTime);

    // ���� ������ �÷����ϰ��ִ��� proxyReadFile ���� �� ���� Ŭ���̾�Ʈ�� �����մϴ�.
    g_songMutex.lock();
    swprintf(message, L"%llx|%s|%lf|%f|%s\n",
        sysTime,
        g_audioPath.c_str(),
        currentTime,
        tempo,
        g_beatmapPath.c_str());
    g_songMutex.unlock();

    g_namedPipe.PushMessage(std::wstring(message));
}

BOOL WINAPI proxyReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    if (!SelectProxy(proxyReadFile).OriginalFunction(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped))
    {
        return FALSE;
    }

    
    TCHAR nameTmpFilePath[MAX_PATH];

    DWORD dwTmpFilePathLength = GetFinalPathNameByHandle(hFile, nameTmpFilePath, MAX_PATH, VOLUME_NAME_DOS);
    DWORD dwFilePosition = SetFilePointer(hFile, 0, NULL, FILE_CURRENT) - (*lpNumberOfBytesRead);

    // ���� //?/�� �����ϱ����� 4���� ������.
    TCHAR* nameFile = &nameTmpFilePath[4];
    DWORD  dwFilePathLength = dwTmpFilePathLength - 4;

    // ���� �д� ������ ��Ʈ�� �����̰� �պκ��� �о��ٸ� ���� ���� ��� ���:
    // ���� �̸��� ���Ե� Path ���κ� 4���ڸ� �ڸ�. 4���ڿ�  .osu�� ���Ͽ� �� ������ osu �������� Ȯ����
    if (wcsncmp(L".osu", &nameFile[dwFilePathLength - 4], 4) == 0 && dwFilePosition == 0)
    {
        // .osu ������ UTF-8(Multibyte) ���ڵ�
        // strtok�� �ҽ��� �����ϹǷ� ���� strdup�� �̿��ؼ� ���ڸ� ������.
        LPSTR buffer = strdup((const char*)(lpBuffer));

        // for���� �̿��ؼ� �ٸ��� strtok���� �߶�.
        for (LPSTR line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n"))
        {
            // �߶� �ٿ� Token�� �ִ��� Ȯ���ϰ� �ƴҰ�쿡�� continue��.
            if (strnicmp(line, AUDIO_FILE_INFO_TOKEN, strlen(AUDIO_FILE_INFO_TOKEN)) != 0)
            {
                continue;
            }

            TCHAR nameAudioFile[MAX_PATH];
            mbstowcs(nameAudioFile, &line[strlen(AUDIO_FILE_INFO_TOKEN)], MAX_PATH);
            StrTrimW(nameAudioFile, L" \r");

            TCHAR pathAudioFile[MAX_PATH];
            wcscpy(pathAudioFile, nameFile);

            // pathAudioFile���� ���ϸ��� ����ϴ�.
            PathRemoveFileSpecW(pathAudioFile);

            // ���ϸ��� ������ Path�� pathAudioFile�� nameAudioFile�ٿ�, ������ Path�� ����ϴ�.
            PathCombineW(pathAudioFile, pathAudioFile, nameAudioFile);

            g_songMutex.lock();
            g_beatmapPath  = (std::wstring(nameFile));
            g_audioPath    = (std::wstring(pathAudioFile));

            if (g_songCached.find(g_audioPath) == g_songCached.end())
            {
                g_songCached.insert(
                    std::pair<std::wstring, std::wstring>(pathAudioFile, nameFile));
            }

            g_songMutex.unlock();

            break;
        }

        // strdup�� �̿��� ������ ���ڿ��� �޸𸮸� ������ŵ�ϴ�.
        free(buffer);
    }
    else
    {
        // Beatmap�� �ٽ� �ҷ����� �ʰ� Osu������ �޸� Cache���� �ҷ��ö��� �ִµ�.                         
        // �׷���쿡�� Audio���ϸ� ������ ������ ������ Beatmap�� Audio������ ������ 
        // �������� �ִ��� Ȯ���ϰ� ���� ����ǰ��ִ� ���� ���� ���� AudioFile���� �ٲ۴�.                            
        g_songMutex.lock();
        auto cachedInfo = g_songCached.find(nameFile);
        
        if (cachedInfo != g_songCached.end())
        {
            g_beatmapPath = cachedInfo->second;
        }

        g_songMutex.unlock();
    }
    return TRUE;
    
}

BOOL WINAPI proxyBASS_ChannelPlay(DWORD handle, BOOL restart)
{
    if (!SelectProxy(proxyBASS_ChannelPlay).OriginalFunction(handle, restart))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);

    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));
        float currentTempo = 0;
        BASS_ChannelGetAttribute(handle, BASS_ATTRIB_TEMPO, &currentTempo);

        //Notify(currentTime, currentTempo);
    }
    return TRUE;

}

BOOL WINAPI proxyBASS_ChannelSetPosition(DWORD handle, QWORD pos, DWORD mode)
{
    if (!SelectProxy(proxyBASS_ChannelSetPosition).OriginalFunction(handle, pos, mode))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);

    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, pos);
        float currentTempo = 0;
        BASS_ChannelGetAttribute(handle, BASS_ATTRIB_TEMPO, &currentTempo);
        // ����!! pos�� ���� ������ ��,
        // ����ϸ� proxyBASS_ChannelPlay��� �� �Լ��� ȣ��ǰ�,
        // BASS_ChannelIsActive ���� BASS_ACTIVE_PAUSED��.
        if (BASS_ChannelIsActive(handle) == BASS_ACTIVE_PAUSED)
        {
            currentTempo = -100;
        }

        Notify(currentTime, currentTempo);
    }

    return TRUE;
}

BOOL WINAPI proxyBASS_ChannelSetAttribute(DWORD handle, DWORD attrib, float value)
{
    if (!SelectProxy(proxyBASS_ChannelSetAttribute).OriginalFunction(handle, attrib, value))
    {
        return FALSE;
    }
    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);

    if ((info.ctype & BASS_CTYPE_STREAM) && attrib == BASS_ATTRIB_TEMPO)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));

        Notify(currentTime, value);
    }

    return TRUE;
}

BOOL WINAPI proxyBASS_ChannelPause(DWORD handle)
{
    if (!SelectProxy(proxyBASS_ChannelPause).OriginalFunction(handle))
    {
        return FALSE;
    }

    BASS_CHANNELINFO info;
    BASS_ChannelGetInfo(handle, &info);
    if (info.ctype & BASS_CTYPE_STREAM)
    {
        double currentTime = BASS_ChannelBytes2Seconds(handle, BASS_ChannelGetPosition(handle, BASS_POS_BYTE));

        Notify(currentTime, -100);
    }

    return TRUE;
}


BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH: 
        // 
        // LoaderLock�� ������� ���� ���ؼ� ������� ȣ���Ѵ�.
        //
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Start, 0, 0, 0);
        break;
    case DLL_PROCESS_DETACH: 
        Stop(); 
        break;
    }
    return TRUE;
}
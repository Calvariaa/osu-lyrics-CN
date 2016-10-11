#pragma once

#include "Defines.h"
#include "detours.h"
#include "bass.h"
#include "bass_fx.h"

#include <atomic>
#include <thread>
#include <concurrent_queue.h>

//
// proxy ��� ���Ǻ�.
//

BOOL WINAPI 
proxyReadFile(                  HANDLE hFile, 
                                LPVOID lpBuffer, 
                                DWORD nNumberOfBytesToRead, 
                                LPDWORD lpNumberOfBytesRead, 
                                LPOVERLAPPED lpOverlapped);
BOOL WINAPI 
proxyBASS_ChannelPlay(          DWORD handle, 
                                BOOL restart);
BOOL WINAPI 
proxyBASS_ChannelSetPosition(   DWORD handle, 
                                QWORD pos, 
                                DWORD mode);
BOOL WINAPI 
proxyBASS_ChannelSetAttribute(  DWORD handle, 
                                DWORD attrib, 
                                float value);
BOOL WINAPI 
proxyBASS_ChannelPause(         DWORD handle);

#define MAKE_PROXY_DEF(module, function, proxy)             \
    HookEngine<decltype(proxy)> proxy##__hk =               \
    HookEngine<decltype(proxy)>(module, function, proxy)
#define SelectProxy(proxy_class) proxy_class##__hk
#define EngineHook(proxy_class)                             \
    SelectProxy(proxy_class).TryHook()
#define EngineUnhook(proxy_class)                           \
    SelectProxy(proxy_class).Unhook()
#define BeginHook()                                         \
    do                                                      \
    {                                                       \
        DetourTransactionBegin();                           \
        DetourUpdateThread(GetCurrentThread());             \
    } while(0)
#define EndHook() DetourTransactionCommit()

#define NAME_BASS_DLL                   L"bass.dll"
#define NAME_KERNEL_DLL                 L"kernel32.dll"
#define NAME_NAMED_PIPE                 L"\\\\.\\pipe\\osu!Lyrics"
#define AUDIO_FILE_INFO_TOKEN           "AudioFilename:"
#define MAX_MESSAGE_LENGTH              600

template <class funcType>
class HookEngine
{
private:
    funcType* proxy;
    bool isHooked;

public:
    funcType* OriginalFunction;

    HookEngine(const wchar_t *nameModule, const char *nameFunction, funcType* proxy)
    {
        this->OriginalFunction = (funcType*)GetProcAddress(GetModuleHandle(nameModule), nameFunction);
        this->proxy = proxy;
    }
    void TryHook()
    {
        if (!isHooked) this->isHooked = !!DetourAttach(&(PVOID&)OriginalFunction, (PVOID)proxy);
    }
    void Unhook()
    {
        if (isHooked) this->isHooked = !DetourDetach(&(PVOID&)OriginalFunction, (PVOID)proxy);
    }
};

class NamedPipe
{
    HANDLE                                      m_hEvent;
    HANDLE                                      m_hPipe;

    std::atomic<bool>                           m_isThreadRunning;
    std::atomic<bool>                           m_isPipeConnected;

    std::thread*                                m_ThreadObject;
    concurrency::concurrent_queue<std::wstring> m_ThreadQueues;

public:


    void Start(const std::wstring&& nPipe)
    {

        m_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        m_hPipe  = CreateNamedPipeW(

            nPipe.c_str(), 

            PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, MAX_MESSAGE_LENGTH, 0, INFINITE, NULL);

        m_isThreadRunning = true;
        
        m_ThreadObject = new std::thread([this]() {
            std::wstring wMessage;
            DWORD        nWritten;

            while (m_isThreadRunning)
            {
                //
                // ConnectNamedPipe�� Ŭ���̾�Ʈ�� �����\ ������ ���� �����:
                // ��Ҵ� DisconnectNamedPipe�� ����
                //
                if (ConnectNamedPipe(m_hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)
                {
                    m_isPipeConnected = true;

                    if (!m_ThreadQueues.try_pop(wMessage))
                    {
                        WaitForSingleObject(m_hEvent, 3000);
                        continue;
                    }

                    if (WriteFile(m_hPipe, wMessage.c_str(), wMessage.length() * sizeof(std::wstring::value_type), &nWritten, NULL))
                    {
                        continue;
                    }

                    m_isPipeConnected = false;
                    DisconnectNamedPipe(m_hPipe);
                }
            }

            m_isPipeConnected = false;
            DisconnectNamedPipe(m_hPipe);
            CloseHandle(m_hPipe);
        });

        return;
    }

    void Stop()
    {
        m_isThreadRunning = false;
        m_ThreadObject->join();
        delete m_ThreadObject;

        CloseHandle(m_hEvent);
    }

    void PushMessage(const std::wstring&& message)
    {
        if (!(m_isPipeConnected & m_isThreadRunning))
        {
            return;
        }

        m_ThreadQueues.push(message);
        SetEvent(m_hEvent);
    }
};
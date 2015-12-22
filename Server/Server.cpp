#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <string>
#include "ConcurrentQueue.h"
#include "Observer.h"
#include "Server.h"

ConcurrentQueue<std::string> MessageQueue;

HANDLE hPipeThread;
HANDLE hPipe;
volatile bool bCancelPipeThread;
volatile bool bPipeConnected;
DWORD WINAPI PipeThread(LPVOID lParam)
{
    hPipe = CreateNamedPipe("\\\\.\\pipe\\osu!Lyrics", PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, BUF_SIZE, 0, INFINITE, NULL);
    std::string message;
    // ������ ���� ��û�� ���� ������ Ŭ���̾�Ʈ ���� ���� ���
    while (!bCancelPipeThread)
    {
        // ConnectNamedPipe�� Ŭ���̾�Ʈ�� ����� ������ ���� �����:
        // ��Ҵ� DisconnectNamedPipe�� ����
        BOOL initialized = ConnectNamedPipe(hPipe, NULL);
        if (initialized || GetLastError() == ERROR_PIPE_CONNECTED)
        {
            if (initialized)
            {
                // ���α׷� �ٽ� ������ �� ���� �޽��� �ٷ� ����
                MessageQueue.Push(message);
            }

            bPipeConnected = true;

            if (MessageQueue.Empty())
            {
                // �޼��� ť�� ����� �� 3�ʰ� ��ٷ��� ��ȣ�� ������ �ٽ� ��ٸ�
                MessageQueue.WaitPush(3000);
                continue;
            }

            DWORD wrote;
            message = MessageQueue.Pop();
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

void PushMessage(const std::string &message)
{
    if (!bPipeConnected)
    {
        return;
    }

    MessageQueue.Push(message);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        hPipeThread = CreateThread(NULL, 0, PipeThread, NULL, 0, NULL);

        RunObserver();
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        StopObserver();

        bCancelPipeThread = true;
        DisconnectNamedPipe(hPipe);
        WaitForSingleObject(hPipeThread, INFINITE);
        CloseHandle(hPipeThread);
    }
    return TRUE;
}

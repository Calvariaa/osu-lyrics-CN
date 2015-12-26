#include <Windows.h>
#include <string>
#include <concurrent_queue.h>
#include "Observer.h"
#include "Server.h"

concurrency::concurrent_queue<std::string> MessageQueue;
HANDLE hPushEvent;

HANDLE hServerThread;
HANDLE hPipe;
volatile bool bCancelServerThread;
volatile bool bPipeConnected;
DWORD WINAPI ServerThread(LPVOID lParam)
{
    hPipe = CreateNamedPipe("\\\\.\\pipe\\osu!Lyrics", PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, BUF_SIZE, 0, INFINITE, NULL);
    std::string message;
    DWORD wrote;
    // ������ ���� ��û�� ���� ������ Ŭ���̾�Ʈ ���� ���� ���
    while (!bCancelServerThread)
    {
        // ConnectNamedPipe�� Ŭ���̾�Ʈ�� ����� ������ ���� �����:
        // ��Ҵ� DisconnectNamedPipe�� ����
        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)
        {
            bPipeConnected = true;

            // �޽��� ť�� ����� �� �ִ� 3�ʰ� ��ٸ��� �ٽ� �õ�:
            // Ŭ���̾�Ʈ ������ ����ؾ� �ϱ� ������ INTINITE ����
            if (!MessageQueue.try_pop(message))
            {
                WaitForSingleObject(hPushEvent, 3000);
                continue;
            }

            if (WriteFile(hPipe, message.c_str(), message.length(), &wrote, NULL))
            {
                continue;
            }
        }
        bPipeConnected = false;
        DisconnectNamedPipe(hPipe);
    }
    // Ŭ���̾�Ʈ ���� ����
    bPipeConnected = false;
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    return 0;
}

void PushMessage(std::string &&message)
{
    if (!bPipeConnected)
    {
        return;
    }

    MessageQueue.push(message);
    SetEvent(hPushEvent);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        hPushEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

        hServerThread = CreateThread(NULL, 0, ServerThread, NULL, 0, NULL);

        RunObserver();
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        StopObserver();

        bCancelServerThread = true;
        DisconnectNamedPipe(hPipe);
        WaitForSingleObject(hServerThread, INFINITE);
        CloseHandle(hServerThread);

        CloseHandle(hPushEvent);
    }
    return TRUE;
}

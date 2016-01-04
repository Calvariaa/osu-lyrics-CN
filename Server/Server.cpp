#include "Server.h"

#include <tchar.h>
#include <string>
#include <concurrent_queue.h>

#include <Windows.h>
#include "Observer.h"

concurrency::concurrent_queue<tstring> MessageQueue;
HANDLE hPushEvent;

HANDLE hServerThread;
HANDLE hPipe;
volatile bool bCancelServerThread;
volatile bool bPipeConnected;
DWORD WINAPI ServerThread(LPVOID lParam)
{
    hPipe = CreateNamedPipe(_T("\\\\.\\pipe\\osu!Lyrics"), PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, nBufferSize * sizeof(tstring::value_type), 0, INFINITE, NULL);
    tstring message;
    DWORD nNumberOfBytesWritten;
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

            if (WriteFile(hPipe, message.c_str(), message.length() * sizeof(tstring::value_type), &nNumberOfBytesWritten, NULL))
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

void PushMessage(tstring &&message)
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

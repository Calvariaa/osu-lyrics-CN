#include "Server.h"

#include "Observer.h"

DWORD WINAPI Server::Thread(LPVOID lParam)
{
    InstanceServer.hPipe = CreateNamedPipe(L"\\\\.\\pipe\\osu!Lyrics", PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, Server::nBufferSize, 0, INFINITE, NULL);

    std::wstring message;
    DWORD nNumberOfBytesWritten;
    // ������ ���� ��û�� ���� ������ Ŭ���̾�Ʈ ���� ���� ���
    while (!InstanceServer.isThreadCanceled)
    {
        // ConnectNamedPipe�� Ŭ���̾�Ʈ�� ����� ������ ���� �����:
        // ��Ҵ� DisconnectNamedPipe�� ����
        if (ConnectNamedPipe(InstanceServer.hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)
        {
            InstanceServer.isPipeConnected = true;

            // �޽��� ť�� ����� �� �ִ� 3�ʰ� ��ٸ��� �ٽ� �õ�:
            // Ŭ���̾�Ʈ ������ ����ؾ� �ϱ� ������ INTINITE ����
            if (!InstanceServer.messageQueue.try_pop(message))
            {
                WaitForSingleObject(InstanceServer.hPushEvent, 3000);
                continue;
            }

            if (WriteFile(InstanceServer.hPipe, message.c_str(), message.length() * sizeof(std::wstring::value_type), &nNumberOfBytesWritten, NULL))
            {
                continue;
            }
        }
        InstanceServer.isPipeConnected = false;
        DisconnectNamedPipe(InstanceServer.hPipe);
    }
    // Ŭ���̾�Ʈ ���� ����
    InstanceServer.isPipeConnected = false;
    DisconnectNamedPipe(InstanceServer.hPipe);
    CloseHandle(InstanceServer.hPipe);
    return 0;
}

void Server::PushMessage(std::wstring&& message)
{
    if (!this->isPipeConnected)
    {
        return;
    }

    this->messageQueue.push(message);
    SetEvent(this->hPushEvent);
}

void Server::Run()
{
    this->hPushEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    this->hThread = CreateThread(NULL, 0, Server::Thread, NULL, 0, NULL);
}

void Server::Stop()
{
    this->isThreadCanceled = true;
    DisconnectNamedPipe(this->hPipe);
    WaitForSingleObject(this->hThread, INFINITE);
    CloseHandle(this->hThread);

    CloseHandle(this->hPushEvent);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        InstanceServer.Run();
        InstanceObserver.Start();
    }
    else if (fdwReason == DLL_PROCESS_DETACH)
    {
        InstanceObserver.Stop();
        InstanceServer.Stop();
    }
    return TRUE;
}

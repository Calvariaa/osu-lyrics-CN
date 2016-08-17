#include "Server.h"

#include <string>

#include <Windows.h>

DWORD WINAPI Server::Run(LPVOID lParam)
{
    this->hPushEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    this->hPipe = CreateNamedPipe(L"\\\\.\\pipe\\osu!Lyrics", PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_MESSAGE | PIPE_WAIT, 1, Server::nBufferSize, 0, INFINITE, NULL);
    std::wstring message;
    DWORD nNumberOfBytesWritten;
    // ������ ���� ��û�� ���� ������ Ŭ���̾�Ʈ ���� ���� ���
    while (!this->isCancellationRequested)
    {
        // ConnectNamedPipe�� Ŭ���̾�Ʈ�� ����� ������ ���� �����:
        // ��Ҵ� DisconnectNamedPipe�� ����
        if (ConnectNamedPipe(this->hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)
        {
            this->isPipeConnected = true;

            // �޽��� ť�� ����� �� �ִ� 3�ʰ� ��ٸ��� �ٽ� �õ�:
            // Ŭ���̾�Ʈ ������ ����ؾ� �ϱ� ������ INTINITE ����
            if (!this->messageQueue.try_pop(message))
            {
                WaitForSingleObject(this->hPushEvent, 3000);
                continue;
            }

            if (WriteFile(this->hPipe, message.c_str(), message.length() * sizeof(std::wstring::value_type), &nNumberOfBytesWritten, NULL))
            {
                continue;
            }
        }
        this->isPipeConnected = false;
        DisconnectNamedPipe(this->hPipe);
    }
    // Ŭ���̾�Ʈ ���� ����
    this->isPipeConnected = false;
    DisconnectNamedPipe(this->hPipe);
    CloseHandle(this->hPipe);

    CloseHandle(this->hPushEvent);
    return 0;
}

namespace bettertrunkneeded_maybetemplatetrunk_question
{
    DWORD WINAPI trunk(LPVOID lParam)
    {
        Server *server = (Server *) lParam;
        return server->Run(nullptr);
    }
}

void Server::Start()
{
    this->hThread = CreateThread(NULL, 0, bettertrunkneeded_maybetemplatetrunk_question::trunk, this, 0, NULL);
}

void Server::Stop()
{
    this->isCancellationRequested = true;
    DisconnectNamedPipe(this->hPipe);
    WaitForSingleObject(this->hThread, INFINITE);
    CloseHandle(this->hThread);
}

void Server::Update(std::wstring&& message)
{
    if (!this->isPipeConnected)
    {
        return;
    }

    this->messageQueue.push(message);
    SetEvent(this->hPushEvent);
}

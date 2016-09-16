#include "Server.h"

#include <string>

#include <Windows.h>

Server::Server() :
    isCancellationRequested(false),
    hPipe(NULL),
    isPipeConnected(false),
    hPushEvent(NULL)
{
}

void Server::Run()
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
}

void Server::Stop()
{
    this->isCancellationRequested = true;
    DisconnectNamedPipe(this->hPipe);
}

void Server::Update(const std::wstring& message)
{
    if (!this->isPipeConnected)
    {
        return;
    }

    this->messageQueue.push(message);
    SetEvent(this->hPushEvent);
}

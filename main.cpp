// ChatServer.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include "pch.h"
#include "Socket.h"

Socket gSocket;
int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);


   // socket = new Socket();

    if (gSocket.Initialize() == false)
        return EXIT_FAILURE;

    if (gSocket.ServerProcess() == false)
        return EXIT_FAILURE;

    //SafeDelete(gSocket);

    return EXIT_SUCCESS;
}

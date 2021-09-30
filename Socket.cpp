#include "pch.h"
#include "Socket.h"
#include "Protocol.h"
#include "RingBuffer.h"
#include "PacketBuffer.h"

Socket::~Socket()
{
	closesocket(mListenSock);
	WSACleanup();
	UserDataClear();
	SafeDelete(mPacketBuffer);
}

bool Socket::Initialize()
{
	WSADATA wsaData;
	SOCKADDR_IN serveraddr;
	WCHAR serverIP[IP_BUFFER_SIZE] = { 0, };
	int returnValue = 0;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		wprintf(L"WSAStartup() errcode[%d]\n", WSAGetLastError());
		return false;
	}

	mListenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == mListenSock)
	{
		wprintf(L"listen_sock error:%d ", WSAGetLastError());
		return false;
	}

	// bind
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVER_PORT);
	InetNtop(AF_INET, &serveraddr.sin_addr, serverIP, _countof(serverIP));

	wprintf(L"\n[CHAT SERVER] SERVER IP: %s SERVER Port:%d\n", serverIP, ntohs(serveraddr.sin_port));

	returnValue = bind(mListenSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (returnValue == SOCKET_ERROR)
	{
		wprintf(L"bind error:%d ", WSAGetLastError());
		return false;
	}

	//listen
	returnValue = listen(mListenSock, SOMAXCONN);
	if (returnValue == SOCKET_ERROR)
	{
		wprintf(L"listen error:%d ", WSAGetLastError());
		return false;
	}
	wprintf(L"server open\n");

	// 논블록킹 소켓으로 전환
	u_long on = 1;
	if (SOCKET_ERROR == ioctlsocket(mListenSock, FIONBIO, &on))
	{
		wprintf(L"ioctlsocket() errcode[%d]\n", WSAGetLastError());
		return false;
	}

	// packetBuffer 할당
	mPacketBuffer = new PacketBuffer;
	_ASSERT(mPacketBuffer != nullptr);
	mPacketBuffer->Initialize();

	return true;
}

void Socket::UserDataClear()
{
	ClientInfo* clientInfo = nullptr;

	for (auto userData : mUserData)
	{
		clientInfo = userData.second;
		SafeDelete(clientInfo->recvRingBuffer);
		SafeDelete(clientInfo->sendRingBuffer);
		SafeDelete(clientInfo);
	}
	mUserData.clear();
}

void Socket::ServerProcess()
{
	fd_set read_set;
	fd_set write_set;
	ClientInfo* clientInfo = nullptr;
	BYTE socketCount = 0;
	list<WORD> userID_Data;

	while (true)
	{
		// 소켓 셋 초기화
		FD_ZERO(&read_set);
		FD_ZERO(&write_set);
		// 리슨 소켓 셋
		FD_SET(mListenSock, &read_set);

		// 유저 데이터 초기화
		userID_Data.clear();
		socketCount = 0;

		// 클라이언트 소켓 셋
		for (auto userData : mUserData)
		{
			clientInfo = userData.second;
			FD_SET(clientInfo->clientSock, &read_set);
			userID_Data.emplace_back(clientInfo->userID);

			if (clientInfo->sendRingBuffer->GetUseSize() > 0)
			{
				FD_SET(clientInfo->clientSock, &write_set);
			}

			// 64개씩 끊어서 select 실행
			if (socketCount >= FD_SETSIZE)
			{
				socketCount = 0;
				SelectSocket(read_set, write_set, userID_Data);

				// 소켓 셋 초기화
				FD_ZERO(&read_set);
				FD_ZERO(&write_set);
				// 소켓 리스트 초기화
				userID_Data.clear();
				// 리슨 소켓 셋
				FD_SET(mListenSock, &read_set);
			}
			else
			{
				++socketCount;
			}
		}
		// 64개 미만인 select 실행
		SelectSocket(read_set, write_set, userID_Data);
	}
}

void Socket::SelectSocket(fd_set& read_set, fd_set& write_set, const list<WORD>& userID_Data)
{
	TIMEVAL timeout;
	int fdNum;
	int addrlen;
	int returnVal;
	SOCKET clientSock = INVALID_SOCKET;
	WCHAR clientIP[IP_BUFFER_SIZE] = { 0, };
	SOCKADDR_IN clientaddr;
	char inputData[RingBuffer::MAX_BUFFER_SIZE] = { 0, };
	ClientInfo* clientInfo = nullptr;

	// select 즉시 리턴
	timeout.tv_sec = 0;
	timeout.tv_sec = 0;

	fdNum = select(0, &read_set, &write_set, 0, &timeout);

	if (fdNum == 0)
		return;
	else if (fdNum == SOCKET_ERROR)
	{
		wprintf(L"SOCKET_ERROR() errcode[%d]\n", WSAGetLastError());
		return;
	}
	// 소켓 셋 검사
	if (FD_ISSET(mListenSock, &read_set))
	{
		addrlen = sizeof(clientaddr);
		clientSock = accept(mListenSock, (SOCKADDR*)&clientaddr, &addrlen);
		if (clientSock == INVALID_SOCKET)
		{
			wprintf(L"accept error:%d\n", WSAGetLastError());
			return;
		}
		InetNtop(AF_INET, &clientaddr.sin_addr, clientIP, 16);
		wprintf(L"\n[TCP SERVER] Client IP: %s Clinet Port:%d\n", clientIP, ntohs(clientaddr.sin_port));

		// 소켓 정보 추가
		AddSessionInfo(clientSock, clientIP, ntohs(clientaddr.sin_port));
	}

	for (const WORD& userID : userID_Data)
	{
		auto userData = mUserData.find(userID);
		if (userData == mUserData.end())
		{
			wprintf(L"userId find error[userID:%d]\n", userID);
			return;
		}

		clientInfo = userData->second;
		_ASSERT(clientInfo != nullptr);

		if (FD_ISSET(clientInfo->clientSock, &read_set))
		{
			if (returnVal = recv(clientInfo->clientSock, inputData, clientInfo->recvRingBuffer->GetFreeSize(), 0))
			{
				wprintf(L"recv packet size:%d\n", returnVal);

				if (returnVal == SOCKET_ERROR)
				{
					if (WSAGetLastError() != WSAEWOULDBLOCK)
					{
						wprintf(L"recv ERROR errcode[%d]\n", WSAGetLastError());
						continue;						
					}
				}

				returnVal = clientInfo->recvRingBuffer->Enqueue(inputData, returnVal);
				if (returnVal == RingBuffer::USE_COUNT_OVER_FLOW)
				{
					wprintf(L"recv enqueue USE_COUNT_OVER_FLOW returnVal[%d]", returnVal);
					continue;
				}
				LoadRecvRingBuf(clientInfo);
				/*		headerType = *(int*)socketInfo->recvBuffer;

						switch (headerType)
						{
						case HEADER_STAR_MOVE:
						{
							packetStruct = *(PACKET_STRUCT*)socketInfo->recvBuffer;
							if (true == StarMove(packetStruct))
							{
								SendStarMove(i, packetStruct);
							}
						}
						break;
						default:
							wprintf(L"default error![%d]", headerType);
							return;
						}*/
			}
			else
			{
				//RemoveSocketInfo(i);
			}
		}
	}

}

void Socket::AddSessionInfo(const SOCKET clientSock, const WCHAR* ip, const WORD port)
{
	ClientInfo* clientInfo = nullptr;

	clientInfo = new ClientInfo;
	_ASSERT(clientInfo != nullptr);

	clientInfo->clientSock = clientSock;
	wcscpy_s(clientInfo->ip, _countof(clientInfo->ip), ip);
	clientInfo->port = port;
	clientInfo->userID = ++mUserIDNum;
	clientInfo->recvRingBuffer = new RingBuffer;
	clientInfo->sendRingBuffer = new RingBuffer;

	mUserData.emplace(clientInfo->userID, clientInfo);
}

void Socket::LoadRecvRingBuf(const ClientInfo* clientInfo)
{
	WORD length = 0;
	DWORD returnVal = 0;
	HeaderInfo header;

	while (clientInfo->recvRingBuffer->GetUseSize() >= sizeof(header))
	{
		// 헤더 패킷 체크 및 출력
		returnVal = clientInfo->recvRingBuffer->Peek((char*)&header, sizeof(header));

		if (returnVal != sizeof(header))
		{
			wprintf(L"Header dequeue size Error [returnVal:%d]", returnVal);
			return;
		}

		if (header.code != PACKET_CODE)
		{
			wprintf(L"Header unknown code error![code:%d]", header.code);
			return;
		}

		//헤더와 페이로드 사이즈가 합친 사이즈보다 적으면 다음 수행을 할 수 없다. 다음에 처리 진행
		if (header.payLoadSize + sizeof(header) > clientInfo->recvRingBuffer->GetUseSize())
		{
			return;
		}

		wprintf(L"Dequeue Header Type:%d RingBuf useSize:%d\n", header.msgType, clientInfo->recvRingBuffer->GetUseSize());
		clientInfo->recvRingBuffer->MoveReadPos(returnVal);

		// 패킷버퍼에 payload 입력
		mPacketBuffer->Clear();
		returnVal = clientInfo->recvRingBuffer->Peek(mPacketBuffer->GetBufferPtr(), header.payLoadSize);

		if (returnVal != header.payLoadSize)
		{
			wprintf(L"Dequeue payLoadSize  size Error [returnVal:%d][payloadSize:%d]", returnVal, header.payLoadSize);
			return;
		}
		// 패킷 버퍼도 버퍼에 직접담은 부분이기 때문에 writepos을 직접 이동시켜준다.
		mPacketBuffer->MoveWritePos(returnVal);

		if (false == MakeCheckSum(header.checkSum, header.msgType, mPacketBuffer->GetDataSize()))
		{
			wprintf(L"CheckSum Error! [checksum:%d msgType:%d packetSize:%d]", header.checkSum, header.msgType, mPacketBuffer->GetDataSize());
			return;
		}
		wprintf(L"Dequeue data size:%d RingBuf useSize:%d\n", returnVal, clientInfo->recvRingBuffer->GetUseSize());
		clientInfo->recvRingBuffer->MoveReadPos(returnVal);

		PacketProcess(header.msgType, clientInfo);



		//switch (header.msgType)
		//{
		//case HEADER_CS_LOGIN:
		//	{
		//		//CreateMyPlayer(outputData);
		//	}
		//	break;
		//default:
		//	wprintf(L"LoadRecvRingBuf Unkown header[%d]!", header.msgType);
		//	return;
		//}

	}


	

}

bool Socket::MakeCheckSum(const BYTE checkSum, const WORD msgType, const WORD payLoadSize)
{
	BYTE* packetBuffer = (BYTE*)mPacketBuffer->GetBufferPtr();
	WORD checkSumCount = 0;

	for (int i = 0; i < payLoadSize; i++)
	{
		checkSumCount += *packetBuffer;
		packetBuffer++;
	}
	checkSumCount += msgType;
	checkSumCount %= 256;
	if (checkSum != checkSumCount)
		return false;

	return true;
}

void Socket::PacketProcess(const WORD msgType, const ClientInfo* clientInfo)
{
	char tempNameBuffer[NICK_NAME_MAX_LEN * 2];
	WCHAR nickNameBuffer[NICK_NAME_MAX_LEN];

	switch (msgType)
	{
	case HEADER_CS_LOGIN:
		{
			mPacketBuffer->GetData(tempNameBuffer, _countof(tempNameBuffer));
			wcscpy_s(nickNameBuffer, (WCHAR*)tempNameBuffer);
		}
		break;
	default:
		wprintf(L"unknown msgType[%d]", msgType);
		return;
	}
}

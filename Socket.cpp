#include "pch.h"
#include "Socket.h"
#include "../Common\RingBuffer/RingBuffer.h"
#include "../Common\PacketBuffer/PacketBuffer.h"

Socket::~Socket()
{
	closesocket(mListenSock);
	WSACleanup();
	DataClear();
	SafeDelete(mPacketBuffer);
	SafeDelete(mStressString);
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

	// ����ŷ �������� ��ȯ
	u_long on = 1;
	if (SOCKET_ERROR == ioctlsocket(mListenSock, FIONBIO, &on))
	{
		wprintf(L"ioctlsocket() errcode[%d]\n", WSAGetLastError());
		return false;
	}

	// packetBuffer �Ҵ�
	mPacketBuffer = new PacketBuffer;
	_ASSERT(mPacketBuffer != nullptr);
	mPacketBuffer->Initialize();

	// StressBuffer �Ҵ�
	mStressString = new char[1000];
	return true;
}

void Socket::DataClear()
{
	ClientInfo* clientInfo = nullptr;
	RoomInfo* roomInfo = nullptr;

	for (auto userData : mUserData)
	{
		clientInfo = userData.second;
		closesocket(clientInfo->clientSock);
		SafeDelete(clientInfo->recvRingBuffer);
		SafeDelete(clientInfo->sendRingBuffer);
		SafeDelete(clientInfo);
	}
	mUserData.clear();

	for (auto roomData : mRoomData)
	{
		roomInfo = roomData.second;
		SafeDelete(roomInfo);
	}
	mRoomData.clear();

	mNickNameData.clear();
}

bool Socket::ServerProcess()
{
	fd_set read_set;
	fd_set write_set;
	ClientInfo* clientInfo = nullptr;
	BYTE socketCount = 0;
	vector<DWORD> userID_Data;

	// userid �� �ִ� fdsetsize ��ŭ ���� ������ �̸� �޸� �Ҵ� �Ͽ� reallocation ����
	userID_Data.reserve(FD_SETSIZE);

	while (true)
	{
		// ���� �� �ʱ�ȭ
		FD_ZERO(&read_set);
		FD_ZERO(&write_set);
		// ���� ���� ��
		FD_SET(mListenSock, &read_set);

		// ���� ������ �ʱ�ȭ
		userID_Data.clear();
		socketCount = 0;

		// Ŭ���̾�Ʈ ���� ��
		for (auto userData : mUserData)
		{
			clientInfo = userData.second;
			FD_SET(clientInfo->clientSock, &read_set);
			userID_Data.emplace_back(clientInfo->userID);

			if (clientInfo->sendRingBuffer->GetUseSize() > 0)
			{
				FD_SET(clientInfo->clientSock, &write_set);
			}

			// 64���� ��� select ����
			if (socketCount >= FD_SETSIZE)
			{
				socketCount = 0;
				SelectSocket(read_set, write_set, userID_Data);

				// ���� �� �ʱ�ȭ
				FD_ZERO(&read_set);
				FD_ZERO(&write_set);
				// ���� ����Ʈ �ʱ�ȭ
				userID_Data.clear();
				// ���� ���� ��
				FD_SET(mListenSock, &read_set);
			}
			else
			{
				++socketCount;
			}
		}
		// 64�� �̸��� select ����
		SelectSocket(read_set, write_set, userID_Data);
	}

	return false;
}

void Socket::SelectSocket(fd_set& read_set, fd_set& write_set, const vector<DWORD>& userID_Data)
{
	TIMEVAL timeout;
	int fdNum;
	int addrlen;
	int returnVal;
	SOCKET clientSock = INVALID_SOCKET;
	WCHAR clientIP[IP_BUFFER_SIZE] = { 0, };
	SOCKADDR_IN clientaddr;
	ClientInfo* clientInfo = nullptr;
	bool isWritePos = false;

	// select ��� ����
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
	// ���� �� �˻�
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
		wprintf(L"\n[CHAT SERVER] Client IP: %s Clinet Port:%d\n", clientIP, ntohs(clientaddr.sin_port));

		// ���� ���� �߰�
		AddClientInfo(clientSock, clientIP, ntohs(clientaddr.sin_port));
	}

	for (const DWORD& userID : userID_Data)
	{
		auto userData = mUserData.find(userID);
		if (userData == mUserData.end())
		{
			wprintf(L"userId find error[userID:%d]\n", userID);
			continue;
		}

		clientInfo = userData->second;
		_ASSERT(clientInfo != nullptr);
		if (FD_ISSET(clientInfo->clientSock, &read_set))
		{
			if (returnVal = recv(clientInfo->clientSock, clientInfo->recvRingBuffer->GetBufferPtr(), clientInfo->recvRingBuffer->GetNotBroken_WriteSize(), 0))
			{
				wprintf(L"recv packet size:%d\n", returnVal);

				if (returnVal == SOCKET_ERROR)
				{
					// ���� ���� ���� ����
					if (WSAGetLastError() == WSAECONNRESET)
					{
						RemoveClientInfo(clientInfo);
						continue;
					}
					if (WSAGetLastError() != WSAEWOULDBLOCK)
					{
						wprintf(L"recv ERROR errcode[%d]\n", WSAGetLastError());
						continue;						
					}
	
				}

				//returnVal = clientInfo->recvRingBuffer->Enqueue(inputData, returnVal);
				returnVal = clientInfo->recvRingBuffer->MoveWritePos(returnVal);
				if (returnVal == RingBuffer::USE_COUNT_OVER_FLOW)
				{
					wprintf(L"recv enqueue USE_COUNT_OVER_FLOW returnVal[%d]", returnVal);
					continue;
				}
				LoadRecvRingBuf(clientInfo);
			}
			else
			{
				RemoveClientInfo(clientInfo);
				continue;
			}
		}
		if (FD_ISSET(clientInfo->clientSock, &write_set))
		{
			SendRingBuf(clientInfo);
		}

	}

}

void Socket::AddClientInfo(const SOCKET clientSock, const WCHAR* ip, const WORD port)
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

void Socket::RemoveClientInfo(ClientInfo* clientInfo)
{
	DWORD userID = clientInfo->userID;

	// �濡�� �ִٸ� ������ �� ���� ����
	if (clientInfo->roomID != 0)
	{
		RoomLeave(clientInfo);
	}

	// �г��� ����
	auto iterNameSet = mNickNameData.find(clientInfo->nickName);
	if (iterNameSet == mNickNameData.end())
	{
		wprintf(L"RemoveClientInfo nickname find error!");
		return;
	}
	mNickNameData.erase(iterNameSet);

	closesocket(clientInfo->clientSock);
	SafeDelete(clientInfo->recvRingBuffer);
	SafeDelete(clientInfo->sendRingBuffer);
	SafeDelete(clientInfo);
	--mUserIDNum;

	// ���� ������ ����
	auto userData = mUserData.find(userID);
	if (userData == mUserData.end())
	{
		wprintf(L"RemoveClientInfo userData find error!");
		return;
	}
	mUserData.erase(userData);

}

void Socket::LoadRecvRingBuf(ClientInfo* clientInfo)
{
	WORD length = 0;
	DWORD returnVal = 0;
	HeaderInfo header;

	while (clientInfo->recvRingBuffer->GetUseSize() >= sizeof(header))
	{
		// ��� ��Ŷ üũ �� ���
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

		//����� ���̷ε� ����� ��ģ ������� ������ ���� ������ �� �� ����. ������ ó�� ����
		if (header.payLoadSize + sizeof(header) > clientInfo->recvRingBuffer->GetUseSize())
		{
			return;
		}

		wprintf(L"Dequeue Header Type:%d RingBuf useSize:%d\n", header.msgType, clientInfo->recvRingBuffer->GetUseSize());
		clientInfo->recvRingBuffer->MoveReadPos(returnVal);

		// ��Ŷ���ۿ� payload �Է�
		mPacketBuffer->Clear();
		returnVal = clientInfo->recvRingBuffer->Peek(mPacketBuffer->GetBufferPtr(), header.payLoadSize);

		if (returnVal != header.payLoadSize)
		{
			wprintf(L"Dequeue payLoadSize  size Error [returnVal:%d][payloadSize:%d]", returnVal, header.payLoadSize);
			return;
		}
		// ��Ŷ ���۵� ���ۿ� �������� �κ��̱� ������ writepos�� ���� �̵������ش�.
		mPacketBuffer->MoveWritePos(returnVal);

		if (header.checkSum != MakeCheckSum(header.msgType, mPacketBuffer->GetDataSize()))
		{
			wprintf(L"CheckSum Error! [checksum:%d msgType:%d packetSize:%d]", header.checkSum, header.msgType, mPacketBuffer->GetDataSize());
			return;
		}
		wprintf(L"Dequeue data size:%d RingBuf useSize:%d\n", returnVal, clientInfo->recvRingBuffer->GetUseSize());
		clientInfo->recvRingBuffer->MoveReadPos(returnVal);

		PacketProcess(header.msgType, clientInfo);
	}
}

void Socket::SendRingBuf(const ClientInfo* clientInfo)
{
	int retSize = 0;
	int retVal = 0;
	char outputData[RingBuffer::MAX_BUFFER_SIZE] = { 0, };

	if (clientInfo->clientSock == INVALID_SOCKET)
		return;

	if (clientInfo->sendRingBuffer->GetUseSize() <= 0)
		return;

	// ���� �� ���������� ����
	retSize = clientInfo->sendRingBuffer->Peek(outputData, clientInfo->sendRingBuffer->GetUseSize());
	
	retVal = send(clientInfo->clientSock, outputData, retSize, 0);
	wprintf(L"send packet size:%d sendRIngBuffer useSize:%d\n", retVal, clientInfo->sendRingBuffer->GetUseSize());
	if (retVal == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			wprintf(L"Send() WSAGetLastError [errcode:%d][userID:%d]", WSAGetLastError(), clientInfo->userID);
			return;
		}
		// WSAEWOULDBLOCK �����̱� ������ ������ �ƴϹǷ� �ٷ� ����
		wprintf(L"WSAEWOULDBLOCK [userID:%d]", clientInfo->userID);
		return;
	}
	retVal = clientInfo->sendRingBuffer->MoveReadPos(retVal);

	if (retVal == RingBuffer::USE_COUNT_UNDER_FLOW)
	{
		wprintf(L"UseCount Underflow Error!\n");
		return;
	}
	
}

BYTE Socket::MakeCheckSum(const WORD msgType, const WORD payLoadSize)
{
	BYTE* packetBuffer = (BYTE*)mPacketBuffer->GetBufferPtr();
	WORD checkSumCount = 0;

	for (int i = 0; i < payLoadSize; i++)
	{
		checkSumCount += *packetBuffer;
		packetBuffer++;
	}
	checkSumCount += msgType;
	

	return checkSumCount %= 256;
}

void Socket::PacketProcess(const WORD msgType, ClientInfo* clientInfo)
{
	switch (msgType)
	{
	case HEADER_CS_LOGIN:
		{
			LoginRequest(clientInfo);
		}
		break;
	case HEADER_CS_ROOM_LIST:
		{
			RoomList_MakePacket(clientInfo);
		}
		break;
	case HEADER_CS_ROOM_CREATE:
		{
			RoomMake(clientInfo);
		}
		break;
	case HEADER_CS_ROOM_ENTER:
		{
			RoomEnter(clientInfo);
		}
		break;
	case HEADER_CS_CHAT:
		{
			ChatRequest(clientInfo);
		}
		break;
	case HEADER_CS_ROOM_LEAVE:
		{
			RoomLeave(clientInfo);
		}	
		break;
	case HEADER_CS_STRESS_ECHO:
		{
			EchoRequestTest(clientInfo);
		}
		break;
	default:
		wprintf(L"unknown msgType[%d]", msgType);
		return;
	}
}

void Socket::LoginRequest(ClientInfo* clientInfo)
{
	WCHAR nickNameBuffer[NICK_NAME_BUFFER_SIZE];
	mPacketBuffer->GetData((char*)nickNameBuffer, _countof(nickNameBuffer));
	
	if (mNickNameData.size() > 0)
	{
		auto iterNameSet = mNickNameData.find(nickNameBuffer);
		// �ߺ��� �����Ͱ� �ִ��� üũ
		if (iterNameSet != mNickNameData.end())
		{
			// �α��� ��� �� ����
			LoginMakePacket(RESULT_LOGIN_DNICK, clientInfo);
			return;
		}
	}

	// �г��� ����
	mNickNameData.emplace(nickNameBuffer);
	// clientInfo ������ �̸� ����
	wcscpy_s(clientInfo->nickName, nickNameBuffer);

	// �α��� ��� �� ����
	LoginMakePacket(RESULT_LOGIN_OK, clientInfo);
}


void Socket::LoginMakePacket(const BYTE resultMsg, const ClientInfo* clientInfo)
{
	HeaderInfo header;

	mPacketBuffer->Clear();
	*mPacketBuffer << resultMsg << clientInfo->userID;

	header.msgType = HEADER_SC_LOGIN;
	header.code = PACKET_CODE;
	header.payLoadSize = mPacketBuffer->GetDataSize();
	header.checkSum = MakeCheckSum(HEADER_SC_LOGIN, header.payLoadSize);

	SendUnicast(clientInfo, &header);
}

void Socket::RoomList_MakePacket(const ClientInfo* clientInfo)
{
	HeaderInfo header;
	RoomInfo* roomInfo;

	mPacketBuffer->Clear();
	*mPacketBuffer << (WORD)mRoomData.size();

	for (auto iterRoomData : mRoomData)
	{
		roomInfo = iterRoomData.second;
		_ASSERT(roomInfo != nullptr);

		// ��ID, ���̸� ������
		*mPacketBuffer << roomInfo->roomID << roomInfo->roomTitleSize;
		mPacketBuffer->PutData((char*)roomInfo->roomTitle, roomInfo->roomTitleSize);

		// �����ο���
		*mPacketBuffer << (BYTE)roomInfo->userID_Data.size();

		for (DWORD userID : roomInfo->userID_Data)
		{
			auto iterUserData = mUserData.find(userID);
			if (iterUserData == mUserData.end())
			{
				wprintf(L"roominfo userID find error[userID:%d]", userID);
				mPacketBuffer->Clear();
				return;
			}
			mPacketBuffer->PutData((char*)iterUserData->second->nickName, NICK_NAME_BUFFER_SIZE * 2);
		}
	}

	header.msgType = HEADER_SC_ROOM_LIST;
	header.code = PACKET_CODE;
	header.payLoadSize = mPacketBuffer->GetDataSize();
	header.checkSum = MakeCheckSum(HEADER_SC_ROOM_LIST, header.payLoadSize);

	SendUnicast(clientInfo, &header);
}

void Socket::RoomMake(const ClientInfo* clientInfo)
{
	WORD roomTitleSize = 0;
	RoomInfo* roomInfo = nullptr;

	roomInfo = new RoomInfo;
	_ASSERT(roomInfo != nullptr);

	*mPacketBuffer >> roomTitleSize;
	mPacketBuffer->GetData((char*)roomInfo->roomTitle, roomTitleSize);

	if (mRoomData.size() > 0)
	{
		for (auto iterRoomData : mRoomData)
		{
			if (wcscmp(iterRoomData.second->roomTitle, roomInfo->roomTitle) == 0)
			{
				// ����� ��� ���� ����
				RoomMakePacket(RESULT_ROOM_CREATE_DNICK, clientInfo, roomInfo);
				return;
			}
		}
	}
	roomInfo->roomTitleSize = roomTitleSize;
	roomInfo->roomID = ++mRoomIDNum;
	// �����
	mRoomData.emplace(roomInfo->roomID, roomInfo);

	// ����� ��� ���� ����
	RoomMakePacket(RESULT_ROOM_CREATE_OK, clientInfo, roomInfo);
}

void Socket::RoomMakePacket(const BYTE resultMsg, const ClientInfo* clientInfo, const RoomInfo* roomInfo)
{
	HeaderInfo header;

	mPacketBuffer->Clear();
	*mPacketBuffer << resultMsg << roomInfo->roomID << roomInfo->roomTitleSize;
	mPacketBuffer->PutData((char*)roomInfo->roomTitle, roomInfo->roomTitleSize);

	header.code = PACKET_CODE;
	header.msgType = HEADER_SC_ROOM_CREATE;
	header.payLoadSize = mPacketBuffer->GetDataSize();
	header.checkSum = MakeCheckSum(HEADER_SC_ROOM_CREATE, header.payLoadSize);
	
	if (resultMsg == RESULT_ROOM_CREATE_OK)
	{
		SendBroadcast(&header);
	}
	else
	{
		SendUnicast(clientInfo, &header);
	}
	
}

void Socket::RoomEnter(ClientInfo* clientInfo)
{
	DWORD roomID = 0;
	RoomInfo* roomInfo = nullptr;

	*mPacketBuffer >> roomID;
	auto iterRoomData = mRoomData.find(roomID);

	//�����ϴ� ������ üũ
	if (iterRoomData == mRoomData.end())
	{
		RoomEnterPacket(RESULT_ROOM_ENTER_NOT, clientInfo, roomInfo);
		return;
	}
	roomInfo = iterRoomData->second;

	if (roomInfo->userID_Data.size() >= MAX_ROOM_ENTER_NUM)
	{
		RoomEnterPacket(RESULT_ROOM_ENTER_MAX, clientInfo, roomInfo);
		return;
	}


	// Ŭ���̾�Ʈ ������ ���ȣ ����
	clientInfo->roomID = roomInfo->roomID;
	// ��Ŷ���ۿ� send�� ������ ��� ����Ǹ� �ش� �濡 ����ID ����
	roomInfo->userID_Data.emplace_back(clientInfo->userID);

	RoomEnterPacket(RESULT_ROOM_ENTER_OK, clientInfo, roomInfo);
}

void Socket::RoomEnterPacket(const BYTE resultMsg, const ClientInfo* clientInfo, const RoomInfo* roomInfo)
{
	HeaderInfo header;
	mPacketBuffer->Clear();
	// ��� ���� ����
	*mPacketBuffer << resultMsg;

	if (resultMsg == RESULT_ROOM_ENTER_OK)
	{
		*mPacketBuffer << roomInfo->roomID << roomInfo->roomTitleSize;
		mPacketBuffer->PutData((char*)roomInfo->roomTitle, roomInfo->roomTitleSize);
		*mPacketBuffer << (BYTE)roomInfo->userID_Data.size();

		for (DWORD userID : roomInfo->userID_Data)
		{
			auto iterUserData = mUserData.find(userID);
			if (iterUserData == mUserData.end())
			{
				// �������� ������ ���� ��� ����
				mPacketBuffer->Clear();
				wprintf(L"Room Enter UserID find error[userID:%d]", userID);
				return;
			}
			mPacketBuffer->PutData((char*)iterUserData->second->nickName, NICK_NAME_BUFFER_SIZE * 2);
			*mPacketBuffer << iterUserData->second->userID;
		}

		header.msgType = HEADER_SC_ROOM_ENTER;
		header.code = PACKET_CODE;
		header.payLoadSize = mPacketBuffer->GetDataSize();
		header.checkSum = MakeCheckSum(header.msgType, header.payLoadSize);
		SendUnicast(clientInfo, &header);
		UserEnterPacket(clientInfo, roomInfo);
	}
	else
	{
		header.msgType = HEADER_SC_ROOM_ENTER;
		header.code = PACKET_CODE;
		header.payLoadSize = mPacketBuffer->GetDataSize();
		header.checkSum = MakeCheckSum(header.msgType, header.payLoadSize);	
		SendUnicast(clientInfo, &header);
	}
}

void Socket::UserEnterPacket(const ClientInfo* clientInfo, const RoomInfo* roomInfo)
{
	HeaderInfo header;
	mPacketBuffer->Clear();

	mPacketBuffer->PutData((char*)clientInfo->nickName, NICK_NAME_BUFFER_SIZE * 2);
	*mPacketBuffer << clientInfo->userID;

	header.msgType = HEADER_SC_USER_ENTER;
	header.code = PACKET_CODE;
	header.payLoadSize = mPacketBuffer->GetDataSize();
	header.checkSum = MakeCheckSum(header.msgType, header.payLoadSize);

	for (DWORD userID : roomInfo->userID_Data)
	{
		auto iterUserData = mUserData.find(userID);
		if (userID == clientInfo->userID)
			continue;
		
		SendUnicast(iterUserData->second, &header);
	}
}

void Socket::ChatRequest(const ClientInfo* clientInfo)
{
	WORD chatSize = 0;
	wstring chatString;

	*mPacketBuffer >> chatSize;
	mPacketBuffer->GetData((char*)chatString.c_str(), chatSize);

	ChatSendPacket(clientInfo, chatString, chatSize);
}

void Socket::ChatSendPacket(const ClientInfo* clientInfo, const wstring& chatString, const WORD chatSize)
{
	RoomInfo* roomInfo = nullptr;
	HeaderInfo header;
	mPacketBuffer->Clear();

	auto iterRoomInfo = mRoomData.find(clientInfo->roomID);

	if (iterRoomInfo == mRoomData.end())
	{
		wprintf(L"ChatSendpacket room find error()[roomID:%ld]", clientInfo->roomID);
		return;
	}
	roomInfo = iterRoomInfo->second;

	*mPacketBuffer << clientInfo->userID;							// ������ NO
	*mPacketBuffer << chatSize;										// ä�� ������
	mPacketBuffer->PutData((char*)chatString.c_str(), chatSize);	// ä�� ����

	header.msgType = HEADER_SC_CHAT;
	header.code = PACKET_CODE;
	header.payLoadSize = mPacketBuffer->GetDataSize();
	header.checkSum = MakeCheckSum(header.msgType, header.payLoadSize);

	for (DWORD userID : roomInfo->userID_Data)
	{
		auto iterUserData = mUserData.find(userID);
		if (userID == clientInfo->userID)
			continue;

		SendUnicast(iterUserData->second, &header);
	}
}

void Socket::RoomLeave(ClientInfo* clientInfo)
{
	HeaderInfo header;
	RoomInfo* roomInfo = nullptr;
	
	auto iterRommData = mRoomData.find(clientInfo->roomID);
	if (iterRommData == mRoomData.end())
	{
		wprintf(L"RoomLeave roomID find error![roomID:%ld]", clientInfo->roomID);
		return;
	}

	mPacketBuffer->Clear();
	*mPacketBuffer << clientInfo->userID;

	header.code = PACKET_CODE;
	header.msgType = HEADER_SC_ROOM_LEAVE;
	header.payLoadSize = mPacketBuffer->GetDataSize();
	header.checkSum = MakeCheckSum(header.msgType, header.payLoadSize);

	roomInfo = iterRommData->second;
	auto iterUserID = roomInfo->userID_Data.begin();

	for (; iterUserID != roomInfo->userID_Data.end(); )
	{
		auto iterUserData = mUserData.find(*iterUserID);

		if(iterUserData == mUserData.end())
		{
			wprintf(L"RoomLeave userID find error[userID:%d]", *iterUserID);
			mPacketBuffer->Clear();
			return;
		}
		// ������ ���� ���� ����.
		SendUnicast(iterUserData->second, &header);

		// ������ ���� �뵥���Ϳ��� ���� ����.
		if (*iterUserID == clientInfo->userID)
		{
			iterUserID = roomInfo->userID_Data.erase(iterUserID);
			// ���� �濡 ������ ���ٸ� ������� ���� ����
			if (roomInfo->userID_Data.size() == 0)
			{
				RoomDelete(clientInfo->roomID);
				SafeDelete(roomInfo);
				mRoomData.erase(iterRommData);
				return;
			}
			// clientInfo �������� roomID 0���� �ʱ�ȭ ����(���� ���̴� ���� ����)
			clientInfo->roomID = 0; 		
		}
		else
		{
			++iterUserID;
		}
	}
}

void Socket::RoomDelete(const DWORD roomID)
{
	HeaderInfo header;

	mPacketBuffer->Clear();
	*mPacketBuffer << roomID;

	header.code = PACKET_CODE;
	header.msgType = HEADER_SC_ROOM_DELETE;
	header.payLoadSize = mPacketBuffer->GetDataSize();
	header.checkSum = MakeCheckSum(header.msgType, header.payLoadSize);

	SendBroadcast(&header);
}

void Socket::EchoRequestTest(const ClientInfo* clientInfo)
{
	WORD chatSize = 0;
	*mPacketBuffer >> chatSize;

	mPacketBuffer->GetData(mStressString, chatSize);

	EchoMakePacket(clientInfo, chatSize);
}

void Socket::EchoMakePacket(const ClientInfo* clientInfo, const WORD chatSize)
{
	HeaderInfo header;
	mPacketBuffer->Clear();

	*mPacketBuffer << chatSize;										// ä�� ������
	mPacketBuffer->PutData(mStressString, chatSize);	// ä�� ����

	header.msgType = HEADER_SC_STRESS_ECHO;
	header.code = PACKET_CODE;
	header.payLoadSize = mPacketBuffer->GetDataSize();
	header.checkSum = MakeCheckSum(header.msgType, header.payLoadSize);

	SendUnicast(clientInfo, &header);
}


void Socket::SendUnicast(const ClientInfo* clientInfo, const HeaderInfo* header)
{
	clientInfo->sendRingBuffer->Enqueue((char*)header, sizeof(HeaderInfo));
	clientInfo->sendRingBuffer->Enqueue(mPacketBuffer->GetBufferPtr(), mPacketBuffer->GetDataSize());
}

void Socket::SendBroadcast(const HeaderInfo* header)
{
	ClientInfo* clientInfo = nullptr;

	for (auto iterUserData : mUserData)
	{
		clientInfo = iterUserData.second;
		SendUnicast(clientInfo, header);
	}
}

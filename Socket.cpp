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

	// StressBuffer 할당
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

	// userid 는 최대 fdsetsize 만큼 들어가기 때문에 미리 메모리 할당 하여 reallocation 방지
	userID_Data.reserve(FD_SETSIZE);

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
		wprintf(L"\n[CHAT SERVER] Client IP: %s Clinet Port:%d\n", clientIP, ntohs(clientaddr.sin_port));

		// 소켓 정보 추가
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
					// 강제 연결 접속 종료
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

	// 방에들어가 있다면 방퇴장 및 삭제 진행
	if (clientInfo->roomID != 0)
	{
		RoomLeave(clientInfo);
	}

	// 닉네임 삭제
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

	// 유저 데이터 삭제
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

	// 보낼 수 있을때까지 보냄
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
		// WSAEWOULDBLOCK 상태이기 때문에 에러는 아니므로 바로 리턴
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
		// 중복된 데이터가 있는지 체크
		if (iterNameSet != mNickNameData.end())
		{
			// 로그인 결과 값 전송
			LoginMakePacket(RESULT_LOGIN_DNICK, clientInfo);
			return;
		}
	}

	// 닉네임 저장
	mNickNameData.emplace(nickNameBuffer);
	// clientInfo 정보에 이름 전달
	wcscpy_s(clientInfo->nickName, nickNameBuffer);

	// 로그인 결과 값 전송
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

		// 방ID, 방이름 사이즈
		*mPacketBuffer << roomInfo->roomID << roomInfo->roomTitleSize;
		mPacketBuffer->PutData((char*)roomInfo->roomTitle, roomInfo->roomTitleSize);

		// 참여인원수
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
				// 방생성 결과 정보 전송
				RoomMakePacket(RESULT_ROOM_CREATE_DNICK, clientInfo, roomInfo);
				return;
			}
		}
	}
	roomInfo->roomTitleSize = roomTitleSize;
	roomInfo->roomID = ++mRoomIDNum;
	// 방생성
	mRoomData.emplace(roomInfo->roomID, roomInfo);

	// 방생성 결과 정보 전송
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

	//존재하는 방인지 체크
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


	// 클라이언트 정보에 방번호 저장
	clientInfo->roomID = roomInfo->roomID;
	// 패킷버퍼에 send할 정보가 모두 저장되면 해당 방에 유저ID 저장
	roomInfo->userID_Data.emplace_back(clientInfo->userID);

	RoomEnterPacket(RESULT_ROOM_ENTER_OK, clientInfo, roomInfo);
}

void Socket::RoomEnterPacket(const BYTE resultMsg, const ClientInfo* clientInfo, const RoomInfo* roomInfo)
{
	HeaderInfo header;
	mPacketBuffer->Clear();
	// 결과 정보 전송
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
				// 유저정보 오류로 오류 결과 전달
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

	*mPacketBuffer << clientInfo->userID;							// 송진자 NO
	*mPacketBuffer << chatSize;										// 채팅 사이즈
	mPacketBuffer->PutData((char*)chatString.c_str(), chatSize);	// 채팅 내용

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
		// 퇴장한 유저 정보 전달.
		SendUnicast(iterUserData->second, &header);

		// 퇴장한 유저 룸데이터에서 삭제 진행.
		if (*iterUserID == clientInfo->userID)
		{
			iterUserID = roomInfo->userID_Data.erase(iterUserID);
			// 남은 방에 유저가 없다면 방삭제도 같이 진행
			if (roomInfo->userID_Data.size() == 0)
			{
				RoomDelete(clientInfo->roomID);
				SafeDelete(roomInfo);
				mRoomData.erase(iterRommData);
				return;
			}
			// clientInfo 정보에서 roomID 0으로 초기화 진행(추후 꼬이는 문제 방지)
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

	*mPacketBuffer << chatSize;										// 채팅 사이즈
	mPacketBuffer->PutData(mStressString, chatSize);	// 채팅 내용

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

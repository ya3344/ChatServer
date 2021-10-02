#pragma once
#include "Protocol.h"

class Socket
{
public:
	Socket() = default;
	~Socket();

public:
	enum INFO_INDEX
	{
		SERVER_PORT = 6000,
		NAME_BUFFER_SIZE = 15,
		IP_BUFFER_SIZE = 16,
		MAX_PACKET_SIZE = 20,
		ROOM_TITLE_BUFFER_SIZE = 256,
		NICK_NAME_BUFFER_SIZE = 15, 
		MAX_ROOM_ENTER_NUM = 6,
	};

	struct ClientInfo
	{
		SOCKET clientSock = INVALID_SOCKET;
		WCHAR ip[IP_BUFFER_SIZE] = { 0, };
		WCHAR nickName[NICK_NAME_BUFFER_SIZE] = { 0, };
		WORD port = 0;
		DWORD userID = 0;
		DWORD roomID = 0;
		class RingBuffer* sendRingBuffer = nullptr;
		class RingBuffer* recvRingBuffer = nullptr;	
	};

	struct RoomInfo
	{
		DWORD roomID = 0;
		WORD roomTitleSize = 0;
		WCHAR roomTitle[ROOM_TITLE_BUFFER_SIZE] = { 0, };
		list<DWORD> userID_Data;
	};

public:
	bool Initialize();
	void ServerProcess();

private:
	void DataClear();
	void SelectSocket(fd_set& read_set, fd_set& write_set, const vector<DWORD>& userID_Data);
	void AddClientInfo(const SOCKET clientSock, const WCHAR* ip, const WORD port);
	void RemoveClientInfo(ClientInfo* clientInfo);
	void LoadRecvRingBuf(ClientInfo* clientInfo);
	void SendRingBuf(const ClientInfo* clientInfo);
	BYTE MakeCheckSum(const WORD msgType, const WORD payLoadSize);
	void PacketProcess(const WORD msgType, ClientInfo* clientInfo);

//Request Related Function
private:
	void LoginRequest(ClientInfo* clientInfo);
	void LoginMakePacket(const BYTE resultMsg, const ClientInfo* clientInfo);
	void RoomList_MakePacket(const ClientInfo* clientInfo);
	void RoomMake(const ClientInfo* clientInfo);
	void RoomMakePacket(const BYTE resultMsg, const ClientInfo* clientInfo, const RoomInfo* roomInfo);
	void RoomEnter(ClientInfo* clientInfo);
	void RoomEnterPacket(const BYTE resultMsg, const ClientInfo* clientInfo, const RoomInfo* roomInfo);
	void UserEnterPacket(const ClientInfo* clientInfo, const RoomInfo* roomInfo);
	void ChatRequest(const ClientInfo* clientInfo);
	void ChatSendPacket(const ClientInfo* clientInfo, const wstring& chatString, const WORD chatSize);
	void RoomLeave(ClientInfo* clientInfo);
	void RoomDelete(const DWORD roomID);
	void EchoRequestTest(const ClientInfo* clientInfo);
	void EchoMakePacket(const ClientInfo* clientInfo, const char* chatString, const WORD chatSize);
//Send RingBuffer Related Function
private:
	void SendUnicast(const ClientInfo* clientInfo, const HeaderInfo* header);
	void SendBroadcast(const HeaderInfo* header);

private:
	DWORD mUserIDNum = 0;
	DWORD mRoomIDNum = 0;
	SOCKET mListenSock = INVALID_SOCKET;
	unordered_map<DWORD, ClientInfo*> mUserData;
	unordered_map<DWORD, RoomInfo*> mRoomData;
	unordered_set<wstring> mNickNameData;
	class PacketBuffer* mPacketBuffer = nullptr;
	char* mStressString = nullptr;
};


#pragma once

class Socket
{
public:
	Socket() = default;
	~Socket();

public:
	enum SOCKET_INDEX
	{
		SERVER_PORT = 6000,
		NAME_BUFFER_SIZE = 15,
		IP_BUFFER_SIZE = 16,
		MAX_PACKET_SIZE = 20,
	};

	struct ClientInfo
	{
		SOCKET clientSock = INVALID_SOCKET;
		WCHAR ip[IP_BUFFER_SIZE];
		WORD port = 0;
		WORD userID = 0;
		WORD roomID = 0;
		class RingBuffer* sendRingBuffer = nullptr;
		class RingBuffer* recvRingBuffer = nullptr;	
	};

public:
	bool Initialize();
	void ServerProcess();

private:
	void UserDataClear();
	void SelectSocket(fd_set& read_set, fd_set& write_set, const list<WORD>& userID_Data);
	void AddSessionInfo(const SOCKET clientSock, const WCHAR* ip, const WORD port);
	void LoadRecvRingBuf(const ClientInfo* clientInfo);
	bool MakeCheckSum(const BYTE checkSum, const WORD msgType, const WORD payLoadSize);
	void PacketProcess(const WORD msgType, const ClientInfo* clientInfo);

private:
	WORD mUserIDNum = 0;
	SOCKET mListenSock = INVALID_SOCKET;
	unordered_map<WORD, ClientInfo*> mUserData;
	//unordered_set<WCHAR, BYTE> mNickNameData;
	class PacketBuffer* mPacketBuffer = nullptr;
};


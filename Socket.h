#pragma once
class Socket
{
public:
	Socket() = default;
	~Socket();

public:
	bool Initialize();


public:
	enum SOCKET_INDEX
	{
		SERVER_PORT = 6000,
		NAME_BUFFER_SIZE = 15,
		IP_BUFFER_SIZE = 16,
		PACKET_CODE = 0x89,
	};

private:
	SOCKET mListenSock = INVALID_SOCKET;
};


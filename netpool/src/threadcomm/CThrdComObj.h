#ifndef _THRD_MSG_OBJ_H
#define _THRD_MSG_OBJ_H

class CThrdComObj  : public CNetRecv {
public:
	CThrdComObj(int thrd_index, uint32_t ipaddr, uint16_t port, int fd) : CNetRecv(ipaddr, port, fd)
	{
		m_thrd_index = thrd_index;
		_LOG_INFO("construct thread comm obj on thread %u", thrd_index);
	}

	virtual ~CThrdComServ()
	{
		_LOG_INFO("destruct thread comm obj on thread %u", m_thrd_index);
	}

private:
	int recv_handle(char *buf, int buf_len);
	
public:
	int m_thrd_index;

	char m_recv_buf[1024];
    int m_recv_len;
}

#endif

#ifndef _THRD_MSG_OBJ_H
#define _THRD_MSG_OBJ_H

#include "thrdComm.h"
#include "CNetRecv.h"

class CThrdComObj  : public CNetRecv {
public:
	CThrdComObj(int thrd_index, uint32_t ipaddr, uint16_t port, int fd) : CNetRecv(ipaddr, port, fd)
	{
		m_recv_len = 0;
		m_thrd_index = thrd_index;
		_LOG_INFO("construct thread comm obj on thread %u", thrd_index);
	}

	virtual ~CThrdComObj()
	{
		_LOG_INFO("destruct thread comm obj on thread %u", m_thrd_index);
	}

private:
	int recv_handle(char *buf, int buf_len);
	
public:
	char m_recv_buf[1024];
    int m_recv_len;
};

#endif

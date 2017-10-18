#ifndef _THRD_COMM_SRV_H
#define _THRD_COMM_SRV_H

#include "thrdComm.h"
#include "CNetAccept.h"

class CThrdComServ  : public CNetAccept {
public:
	CThrdComServ(int thrd_index, const char *local_ipstr, uint16_t port) : CNetAccept(local_ipstr, port)
	{
		m_thrd_index = thrd_index;
		m_thrd_dst_port = 0;
		_LOG_INFO("construct thread comm server on thread %u", thrd_index);
	}

	virtual ~CThrdComServ()
	{
		_LOG_INFO("destruct thread comm server on thread %u", m_thrd_index);
	}

	virtual int init();
	int send_comm_msg(int type, char *buffer, int buffer_len);

	void set_thrd_tid(UTIL_TID tid)
	{
		m_thrd_tid = tid;
		_LOG_INFO("set thread id %lu for thread %d comm server", tid, m_thrd_index);
	}
	UTIL_TID get_thrd_tid()
	{
		return m_thrd_tid;
	}

private:
	int accept_handle(int conn_fd, uint32_t client_ip, uint16_t client_port);

private:
	unsigned short m_thrd_dst_port;
	unsigned long m_thrd_tid;
};

#endif

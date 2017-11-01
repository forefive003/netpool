#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
#include <Ws2tcpip.h>
#include <windows.h>
#include <process.h>
#else
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#endif

#include "netpool.h"
#include "socketwrap.h"
#include "CConnPool.h"

CConnPool::CConnPool(int maxConnCnt)
{
    m_max_conn_cnt = maxConnCnt;
    m_conns_array = new conn_obj_t[maxConnCnt];
    for (int ii = 0; ii < maxConnCnt; ii++)
    {
    	m_conns_array[ii].connObj = NULL;
    #ifdef _WIN32
        m_conns_array[ii].data_lock = 0;
    #else
        pthread_spin_init(&m_conns_array[ii].data_lock, 0);
    #endif
    }
}

CConnPool::~CConnPool()
{
	for(int ii = 0; ii < m_max_conn_cnt; ii++)
    {
        if (m_conns_array[ii].connObj != NULL)
        {
            _LOG_ERROR("connObj %d not delete when destruct CConnPool", ii);
        }
#ifdef _WIN32
        m_conns_array[ii].data_lock = 0;
#else
        pthread_spin_destroy(&m_conns_array[ii].data_lock);
#endif
    } 

	delete []m_conns_array;
}

void CConnPool::lock_index(int index)
{
	assert(index >= 0 && index < m_max_conn_cnt);

#ifdef _WIN32
	while (InterlockedExchange(&m_conns_array[index].data_lock, 1) == 1){
        sleep_s(0);
    }
#else
	pthread_spin_lock(&m_conns_array[index].data_lock);
#endif
}

void CConnPool::unlock_index(int index)
{
	assert(index >= 0 && index < m_max_conn_cnt);

#ifdef _WIN32
	InterlockedExchange(&m_conns_array[index].data_lock, 0);
#else
	pthread_spin_unlock(&m_conns_array[index].data_lock);
#endif
}

int CConnPool::add_conn_obj(int index, CNetRecv *connObj)
{
    assert(index >= 0 && index < m_max_conn_cnt);

    if (m_conns_array[index].connObj != NULL)
    {
        _LOG_ERROR("connPool index %d already has one obj", index);
		assert(0);
        return -1;
    }
    m_conns_array[index].connObj = connObj;
    return 0;
}

void CConnPool::del_conn_obj(int index)
{
    assert(index >= 0 && index < m_max_conn_cnt);

    if (m_conns_array[index].connObj == NULL)
    {
        _LOG_ERROR("connPool index %d has no obj when del.", index);
        return;
    }

    this->lock_index(index);
    m_conns_array[index].connObj = NULL;
    this->unlock_index(index);
    return;
}

int CConnPool::send_on_conn_obj(int index, char *buf, int buf_len)
{
    int ret = 0;
	
	this->lock_index(index);
    if (NULL == m_conns_array[index].connObj)
    {
        this->unlock_index(index);
        return -1;
    }
	ret = m_conns_array[index].connObj->send_data(buf, buf_len);
	this->unlock_index(index);
    return ret;
}

int CConnPool::init()
{
	return 0;
}

void CConnPool::free()
{
	for (int ii = 0; ii < m_max_conn_cnt; ii++)
	{
		this->lock_index(ii);
		if (m_conns_array[ii].connObj != NULL)
		{
			m_conns_array[ii].connObj->free();
		}
		this->unlock_index(ii);
	}
}

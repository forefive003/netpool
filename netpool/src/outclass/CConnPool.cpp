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
    m_free_conns = NULL;
    m_conns_array = new conn_obj_t[maxConnCnt];
    for (int ii = 0; ii < maxConnCnt; ii++)
    {
        /*init node*/
        m_conns_array[ii].next = NULL;
        m_conns_array[ii].index = ii;        
        m_conns_array[ii].connObj = NULL;

#ifndef _WIN32
        pthread_mutexattr_t mux_attr;
        memset(&mux_attr, 0, sizeof(mux_attr));
        pthread_mutexattr_settype(&mux_attr, PTHREAD_MUTEX_RECURSIVE);
        MUTEX_SETUP_ATTR(m_conns_array[ii].data_lock, &mux_attr);
#else
        MUTEX_SETUP(m_conns_array[ii].data_lock);
#endif

        /*add to free list*/
        if (m_free_conns == NULL)
        {
            m_free_conns = &m_conns_array[ii];
        }
        else
        {
            /*add to tail*/
            m_conns_array[ii].next = m_free_conns->next;
            m_free_conns->next = &m_conns_array[ii];
        }
    }

    #ifdef _WIN32
        m_data_lock = 0;
    #else
        pthread_spin_init(&m_data_lock, 0);
    #endif
}

CConnPool::~CConnPool()
{
	for(int ii = 0; ii < m_max_conn_cnt; ii++)
    {
        if (m_conns_array[ii].connObj != NULL)
        {
            _LOG_ERROR("connObj %d not delete when destruct CConnPool", ii);
        }

        MUTEX_CLEANUP(m_conns_array[ii].data_lock);
    } 

	delete []m_conns_array;

#ifdef _WIN32
        m_data_lock = 0;
#else
        pthread_spin_destroy(&m_data_lock);
#endif
}

void CConnPool::lock_index(int index)
{
	assert(index >= 0 && index < m_max_conn_cnt);

    MUTEX_LOCK(m_conns_array[index].data_lock);
}

void CConnPool::unlock_index(int index)
{
	assert(index >= 0 && index < m_max_conn_cnt);

    MUTEX_UNLOCK(m_conns_array[index].data_lock);
}

void CConnPool::lock()
{
#ifdef _WIN32
    while (InterlockedExchange(&m_data_lock, 1) == 1){
        sleep_s(0);
    }
#else
    pthread_spin_lock(&m_data_lock);
#endif
}

void CConnPool::unlock()
{
#ifdef _WIN32
    InterlockedExchange(&m_data_lock, 0);
#else
    pthread_spin_unlock(&m_data_lock);
#endif
}

CNetRecv* CConnPool::get_conn_obj(int index)
{
    return m_conns_array[index].connObj;
}

int CConnPool::add_conn_obj(CNetRecv *connObj)
{
    conn_obj_t *poolNode = NULL;

    this->lock();
    poolNode = m_free_conns;
    if(m_free_conns != NULL)
    {
        m_free_conns = m_free_conns->next;
    }
    this->unlock();

    if (NULL == poolNode)
    {
        return -1;
    }

    assert(poolNode->index >= 0 && poolNode->index < m_max_conn_cnt);

    if (poolNode->connObj != NULL)
    {
		_LOG_ERROR("connPool index %d already has one obj", poolNode->index);
		assert(0);
        return -1;
    }
    poolNode->connObj = connObj;
    _LOG_INFO("add conn obj %p on index %d", connObj, poolNode->index);
    return poolNode->index;
}

void CConnPool::del_conn_obj(int index)
{
    assert(index >= 0 && index < m_max_conn_cnt);

    this->lock_index(index);
    m_conns_array[index].connObj = NULL;

    this->lock();
    m_conns_array[index].next = m_free_conns;
    m_free_conns = &m_conns_array[index];
    this->unlock();

    this->unlock_index(index);

    _LOG_INFO("del conn obj on index %d", index);
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

BOOL CConnPool::is_conn_obj_send_busy(int index)
{
    BOOL ret = 0;
    
    this->lock_index(index);
    if (NULL == m_conns_array[index].connObj)
    {
        this->unlock_index(index);
        return FALSE;
    }
    ret = m_conns_array[index].connObj->is_send_busy();
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

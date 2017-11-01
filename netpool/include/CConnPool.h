#ifndef _CONN_POOL_H
#define _CONN_POOL_H

#include "CNetRecv.h"

typedef struct
{
#ifdef _WIN32
	LONG data_lock;
#else
	pthread_spinlock_t data_lock;
#endif
	CNetRecv *connObj;	
}conn_obj_t;


#ifdef _WIN32

#if  defined(DLL_EXPORT_NP)
class _declspec(dllexport) CConnPool
#elif defined(DLL_IMPORT_NP)
class _declspec(dllimport) CConnPool
#else
class CConnPool
#endif

#else
class CConnPool
#endif

{
public:
	CConnPool(int maxConnCnt);
	virtual ~CConnPool();

public:
	int init();
	void free(); /*disconnect connection, and free*/
	
	void lock_index(int index);
	void unlock_index(int index);

	int add_conn_obj(int index, CNetRecv *connObj);
	void del_conn_obj(int index);

	int send_on_conn_obj(int index, char *buf, int buf_len);

public:
	int m_max_conn_cnt;
	conn_obj_t *m_conns_array;
};

#endif

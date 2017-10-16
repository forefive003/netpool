
#ifndef CNETPOLL_H_
#define CNETPOLL_H_


#define EPOLL_TIMEOUT 1  /*1S*/

class CNetPoll {
public:
	CNetPoll();
	virtual ~CNetPoll();
	static CNetPoll* instance()
    {
        static CNetPoll *netPoll = NULL;

        if(netPoll == NULL)
        {
            netPoll = new CNetPoll();
        }
        return netPoll;
    }
public:
	BOOL _add_listen_job_entity(accept_hdl_func acpt_func,
								int fd, void* param1);
	BOOL _del_listen_job_entity(int fd, free_hdl_func free_func);

	BOOL _add_read_job_entity(read_hdl_func read_func,
					int fd, 
					void* param1,
					unsigned int thrd_index,
					int bufferSize,
					BOOL isTcp);
	BOOL _del_read_job_entity(int  fd, free_hdl_func free_func);

	BOOL _add_write_job_entity(write_hdl_func io_func,
						int  fd, 
						void* param1, 
						unsigned int thrd_index,
						BOOL isTcp);
	BOOL _del_write_job_entity(int  fd, free_hdl_func free_func);

	BOOL _pause_io_reading_evt_entity(int fd);
	BOOL _resume_io_reading_evt_entity(int fd);
	BOOL _del_io_job_entity(int fd, free_hdl_func free_func);

public:	
	BOOL add_listen_job(accept_hdl_func acpt_func,
								int fd, void* param1)
	{
		return this->_add_listen_job_entity(acpt_func, fd, param1);
	}
	BOOL del_listen_job(int fd, free_hdl_func free_func)
	{
		return this->_del_listen_job_entity(fd, free_func);
	}

	BOOL add_read_job(read_hdl_func read_func,
					int fd, 
					void* param1,
					unsigned int thrd_index,
					int bufferSize,
					BOOL isTcp)
	{
		if (thrd_index >= g_ThreadPoolMgr->m_worker_thrd_cnt)
		{
			thrd_index = get_next_thrd_index();
		}

		UTIL_TID tid = util_get_cur_tid();
		if (m_thrdMsgServ_array[thrd_index].get_thrd_tid() == tid)
		{
			return this->_add_read_job_entity(read_func, fd, param1, thrd_index, bufferSize, isTcp);
		}

		MSG_ADD_READ_JOB_T msgAddReadJob;
		msgAddReadJob.read_func = read_func;
		msgAddReadJob.fd 		= fd;
		msgAddReadJob.param1	= param1;
		msgAddReadJob.thrd_index = thrd_index;
		msgAddReadJob.bufferSize = bufferSize;
		msgAddReadJob.isTcp 	 = isTcp;
		if(0 != m_thrdMsgServ_array[thrd_index].send_comm_msg(MSG_ADD_READ_JOB, 
				(char*)&msgAddReadJob, sizeof(msgAddReadJob)))
		{
			return false;
		}
		return true;
	}

	BOOL del_read_job(int  fd, free_hdl_func free_func)
	{
		
	}

	BOOL add_write_job(write_hdl_func io_func,
						int  fd, 
						void* param1, 
						unsigned int thrd_index,
						BOOL isTcp)
	{
		if (thrd_index >= g_ThreadPoolMgr->m_worker_thrd_cnt)
		{
			thrd_index = get_next_thrd_index();
		}
	}
	BOOL del_write_job(int  fd, free_hdl_func free_func);

	BOOL pause_io_reading_evt(int fd);
	BOOL resume_io_reading_evt(int fd);
	BOOL del_io_job(int fd, free_hdl_func free_func);

public:
	void set_debug_func(thrd_init_func init_func,
						thrd_beat_func beat_func,
						thrd_exit_func exit_func);

	BOOL start();
	void let_stop();
	void wait_stop();

	BOOL init_event_fds();
	
	void pause_io_writing_evt(CIoJob *jobNode);
	void pause_io_reading_evt(CIoJob *jobNode);
private:
	static void loop_handle(void *arg, void *param2, void *param3, void *param4);
	unsigned int get_next_thrd_index();

private:
#ifdef _WIN32
    LONG m_index_lock;
#else
    pthread_spinlock_t m_index_lock;
#endif
	unsigned int m_cur_thrd_index;

#ifndef _WIN32
	int  *m_epfd;
#endif

	CThrdComServ *m_thrdMsgServ_array;

	uint32_t m_cur_worker_thrds; /*cur worker thread count, used when stop*/
	MUTEX_TYPE m_lock;

	volatile int m_isShutDown;

	thrd_init_func m_init_func;
	thrd_beat_func m_beat_func;
	thrd_exit_func m_exit_func;
};

extern CNetPoll *g_NetPoll;

#endif 

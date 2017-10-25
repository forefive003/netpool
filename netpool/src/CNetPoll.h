
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
								int fd, void* param1,
								int thrd_index);
	BOOL _del_listen_job_entity(int fd, free_hdl_func free_func, int thrd_index);

	BOOL _add_read_job_entity(read_hdl_func read_func,
					int fd, 
					void* param1,
					int thrd_index,
					int bufferSize,
					BOOL isTcp);
	BOOL _del_read_job_entity(int  fd, free_hdl_func free_func, int thrd_index);

	BOOL _add_write_job_entity(write_hdl_func io_func,
						int  fd, 
						void* param1, 
						int thrd_index,
						BOOL isTcp);
	BOOL _del_write_job_entity(int  fd, free_hdl_func free_func, int thrd_index);

	BOOL _pause_io_reading_evt_entity(int fd, int thrd_index);
	BOOL _resume_io_reading_evt_entity(int fd, int thrd_index);
	BOOL _del_io_job_entity(int fd, free_hdl_func free_func, int thrd_index);

public:	
	BOOL add_listen_job(accept_hdl_func acpt_func,
								int fd, void* param1, int thrd_index);
	BOOL del_listen_job(int fd, free_hdl_func free_func);
	BOOL add_read_job(read_hdl_func read_func,
					int fd, 
					void* param1,
					int thrd_index,
					int bufferSize,
					BOOL isTcp);
	BOOL del_read_job(int  fd, free_hdl_func free_func);
	BOOL add_write_job(write_hdl_func io_func,
						int  fd, 
						void* param1, 
						int thrd_index,
						BOOL isTcp);
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

	BOOL init();
	
	void pause_io_writing_evt(int thrd_index, CIoJob *jobNode);
	void pause_io_reading_evt(int thrd_index, CIoJob *jobNode);

	UTIL_TID get_thrd_tid(int thrd_index);
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

	CThrdComServ **m_thrdMsgServ_array;

	uint32_t m_cur_worker_thrds; /*cur worker thread count, used when stop*/
	MUTEX_TYPE m_lock;

	volatile int m_isShutDown;

	thrd_init_func m_init_func;
	thrd_beat_func m_beat_func;
	thrd_exit_func m_exit_func;
};

extern CNetPoll *g_NetPoll;

#endif 

#ifndef _JOB_IO_MGR_H
#define _JOB_IO_MGR_H

#define  MAX_FD_CNT 65536
#define  MAX_CONNECTION 65536

typedef struct
{
	int fd;
	int thrd_index;

#ifdef _WIN32
	LONG data_lock;
#else
	pthread_spinlock_t data_lock;
#endif
	CIoJob *ioJob;	
}fd_hdl_t;


typedef std::list<int> IOFD_LIST;
typedef IOFD_LIST::iterator IOFD_LIST_Itr;

class CIoJobMgr
{
public:
	CIoJobMgr();
    virtual ~CIoJobMgr();
    static CIoJobMgr* instance()
    {
        static CIoJobMgr *ioJobMgr = NULL;

        if(ioJobMgr == NULL)
        {
            ioJobMgr = new CIoJobMgr();
        }
        return ioJobMgr;
    }

public:
	BOOL init();	
	void add_io_job(int fd, int thrd_index, CIoJob* ioJob);

	void lock_fd(int fd);
	void unlock_fd(int fd);
	int get_fd_thrd_index(int fd);
	CIoJob* get_fd_io_job(int thrd_index, int fd);
	
	int walk_to_set_sets(fd_set *rset, fd_set *wset, fd_set *eset, int thrd_index);
	void walk_to_handle_sets(fd_set *rset, fd_set *wset, fd_set *eset, int thrd_index);
	void handle_deling_job(int thrd_index);

private:
	fd_hdl_t m_fd_array[MAX_FD_CNT];
	IOFD_LIST m_thrd_fds[64];
};

extern CIoJobMgr *g_IoJobMgr;

#endif

#ifndef _JOB_IO_MGR_H
#define _JOB_IO_MGR_H


typedef std::list<CIoJob*> IOJOB_LIST;
typedef IOJOB_LIST::iterator IOJOB_LIST_Itr;

#define  MAX_CONNECTION 65536

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
	CIoJob* find_io_job(int fd);
	void add_io_job(CIoJob* ioJob);
	#if 0
	void del_io_job(CIoJob* ioJob);	
	void move_to_deling_job(CIoJob* ioJob);
	#endif
	
	void lock();
	void unlock();

	int walk_to_set_sets(fd_set *rset, fd_set *wset, fd_set *eset, int thrd_index);
	void walk_to_handle_sets(fd_set *rset, fd_set *wset, fd_set *eset, int thrd_index);
	void handle_deling_job(unsigned int thrd_index);

private:
	IOJOB_LIST *m_io_jobs;

	#ifdef WIN32
	int m_job_thrd_fd_cnt;
	int (*m_job_thrd_fd_array)[MAX_CONNECTION];
	#endif

	#if 0
	IOJOB_LIST m_del_io_jobs;
	#endif
};

extern CIoJobMgr *g_IoJobMgr;

#endif

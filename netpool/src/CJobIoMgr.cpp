
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#endif

#include "netpool.h"
#include "CJobIo.h"
#include "CJobIoMgr.h"

CIoJobMgr *g_IoJobMgr = NULL;

CIoJobMgr::CIoJobMgr() 
{
#ifndef _WIN32
    pthread_mutexattr_t mux_attr;
    memset(&mux_attr, 0, sizeof(mux_attr));
    pthread_mutexattr_settype(&mux_attr, PTHREAD_MUTEX_RECURSIVE);
    MUTEX_SETUP_ATTR(m_job_lock, &mux_attr);
#else
    MUTEX_SETUP_ATTR(m_job_lock, NULL);
#endif
}

CIoJobMgr::~CIoJobMgr()
{
    MUTEX_CLEANUP(m_job_lock);
};

CIoJob* CIoJobMgr::find_io_job(int fd)
{
    IOJOB_LIST_Itr itr;
    CIoJob *pIoJob = NULL;

    for (itr = m_io_jobs.begin();
            itr != m_io_jobs.end();
            itr++)
    {
        pIoJob = *itr;
        if (pIoJob->get_fd() == fd)
        {
            if (pIoJob->get_deleting_flag())
            {
                return NULL;
            }

            return pIoJob;
        }
    }

    return NULL;
}

void CIoJobMgr::add_io_job(CIoJob* ioJob)
{
	MUTEX_LOCK(m_job_lock);
    m_io_jobs.push_back(ioJob);
    MUTEX_UNLOCK(m_job_lock);
}

#if 0
void CIoJobMgr::del_io_job(CIoJob* ioJob)
{
	MUTEX_LOCK(m_job_lock);
    m_io_jobs.remove(ioJob);
    MUTEX_UNLOCK(m_job_lock);
}
#endif

void CIoJobMgr::lock()
{
	MUTEX_LOCK(m_job_lock);
}

void CIoJobMgr::unlock()
{
	MUTEX_UNLOCK(m_job_lock);
}


int CIoJobMgr::walk_to_set_sets(fd_set *rset, fd_set *wset, fd_set *eset, int thrd_index)
{
    IOJOB_LIST_Itr itr;
    CIoJob *pIoJob = NULL;
    int maxFd = 0;

    MUTEX_LOCK(m_job_lock);

    for (itr = m_io_jobs.begin();
            itr != m_io_jobs.end(); )
    {
        pIoJob = *itr;

        /*maybe timenode deleted in callback func, after that, itr not valid*/
        itr++;
        
        int fd = pIoJob->get_fd();
        if (fd > maxFd)
        {
            maxFd = fd;
        }

        if (pIoJob->io_event_read())
        {
            FD_SET(fd, rset);
            FD_SET(fd, eset);
        }
        if (pIoJob->io_event_write())
        {
            FD_SET(fd, wset);
            FD_SET(fd, eset);
        }
#if 0
        else
        {
            _LOG_ERROR("fd event invalid when set.");
        }
#endif
    }

    MUTEX_UNLOCK(m_job_lock);

    return maxFd;
}

void CIoJobMgr::walk_to_handle_sets(fd_set *rset, fd_set *wset, fd_set *eset, int thrd_index)
{
    IOJOB_LIST_Itr itr;
    CIoJob *pIoJob = NULL;

    MUTEX_LOCK(m_job_lock);

    for (itr = m_io_jobs.begin();
            itr != m_io_jobs.end(); )
    {
        pIoJob = *itr;
        
        /*maybe timenode deleted in callback func, after that, itr not valid*/
        itr++;

        if (pIoJob->get_deleting_flag())
        {
            //if deleting, not handle it
            continue;
        }

        int fd = pIoJob->get_fd();

        if (pIoJob->io_event_read())
        {
            if(FD_ISSET(fd, rset))
            {
                pIoJob->read_evt_handle();
            }

            if(FD_ISSET(fd, eset))
            {
                _LOG_WARN("fd %d execption evt comming when has read job", fd);
                pIoJob->read_evt_handle();
            }
        }
        
		if (pIoJob->io_event_write())
        {
            if(FD_ISSET(fd, wset))
            {
                pIoJob->write_evt_handle();
            }

            if(FD_ISSET(fd, eset))
            {
                _LOG_WARN("fd %d execption evt comming when has write job", fd);
                pIoJob->write_evt_handle();
            }
        }
    }

    MUTEX_UNLOCK(m_job_lock);
}

#if 0
void CIoJobMgr::move_to_deling_job(CIoJob* ioJob)
{
    MUTEX_LOCK(m_job_lock);
    m_del_io_jobs.push_back(ioJob);
    MUTEX_UNLOCK(m_job_lock);
}
#endif

void CIoJobMgr::handle_deling_job(unsigned int thrd_index)
{
    IOJOB_LIST_Itr itr;
    IOJOB_LIST deleted_job;
	CIoJob *pIoJob = NULL;

    /*avoid to call free_callback in lock*/
    MUTEX_LOCK(m_job_lock);
    #if 0
    for (itr = m_del_io_jobs.begin();
            itr != m_del_io_jobs.end(); )
    {
        if ((*itr)->get_thrd_index() == thrd_index)
        {
            /*add to deleted list*/            
            deleted_job.push_back(*itr);
            itr = m_del_io_jobs.erase(itr);
        }
        else
        {
            itr++;
        }
    }
    #endif
    for (itr = m_io_jobs.begin();
            itr != m_io_jobs.end(); )
    {
        pIoJob = *itr;
        
        /*maybe timenode deleted in callback func, after that, itr not valid*/
        itr++;

        if (pIoJob->get_thrd_index() == thrd_index)
        {
            if (pIoJob->get_deleting_flag())
            {
                m_io_jobs.remove(pIoJob);
                deleted_job.push_back(pIoJob);
            }
        }
    }
    MUTEX_UNLOCK(m_job_lock);

    /*really free*/
    for (itr = deleted_job.begin();
            itr != deleted_job.end(); )
    {
        /*free resource*/            
        (*itr)->free_callback();
        delete (*itr);

        itr = deleted_job.erase(itr);
    }
    
    return;
}

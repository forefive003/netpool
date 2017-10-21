
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
#include "CThreadPoolMgr.h"

CIoJobMgr *g_IoJobMgr = NULL;

CIoJobMgr::CIoJobMgr() 
{
    for(unsigned int ii = 0; ii < MAX_FD_CNT; ii++)
    {
        m_fd_array[ii].fd = -1;
        m_fd_array[ii].thrd_index = INVALID_THRD_INDEX;
        m_fd_array[ii].ioJob = NULL;
    #ifdef _WIN32
        m_fd_array[ii].data_lock = 0;
    #else
        pthread_spin_init(&m_fd_array[ii].data_lock, 0);
    #endif
    }
}

CIoJobMgr::~CIoJobMgr()
{
    for(unsigned int ii = 0; ii < MAX_FD_CNT; ii++)
    {
        if (m_fd_array[ii].fd != -1)
        {
            _LOG_ERROR("fd %d not delete when destruct ioJobMgr", m_fd_array[ii].fd);
        }
#ifdef _WIN32
        m_fd_array[ii].data_lock = 0;
#else
        pthread_spin_destroy(&m_fd_array[ii].data_lock);
#endif
    }    
};
void CIoJobMgr::lock_fd(int fd)
{
    assert(fd >= 0 && fd < MAX_FD_CNT);

#ifdef _WIN32
    while (InterlockedExchange(&m_fd_array[fd].data_lock, 1) == 1){
        sleep_s(0);
    }
#else
    pthread_spin_lock(&m_fd_array[fd].data_lock);
#endif
}

void CIoJobMgr::unlock_fd(int fd)
{
    assert(fd >= 0 && fd < MAX_FD_CNT);

#ifdef _WIN32
    InterlockedExchange(&m_fd_array[fd].data_lock, 0);
#else
    pthread_spin_unlock(&m_fd_array[fd].data_lock);
#endif
}

int CIoJobMgr::get_fd_thrd_index(int fd)
{
    assert(fd >= 0 && fd < MAX_FD_CNT);

    return m_fd_array[fd].thrd_index;
}

CIoJob* CIoJobMgr::get_fd_io_job(int thrd_index, int fd)
{
    assert(fd >= 0 && fd < MAX_FD_CNT);

    if (m_fd_array[fd].thrd_index == INVALID_THRD_INDEX)
    {
        return NULL;
    }

    if (m_fd_array[fd].thrd_index != thrd_index)
    {
        _LOG_ERROR("fd %d's ioJob not in thrd %d, but %d", fd, thrd_index, m_fd_array[fd].thrd_index);
        return NULL;
    }
    return m_fd_array[fd].ioJob;
}

void CIoJobMgr::add_io_job(int fd, int thrd_index, CIoJob* ioJob)
{
    assert(fd >= 0 && fd < MAX_FD_CNT);
    assert(thrd_index >= 0 && thrd_index < MAX_THRD_CNT);

    if ((m_fd_array[fd].ioJob != NULL) || (m_fd_array[fd].fd != -1) )
    {
        _LOG_ERROR("fd %d already has one ioJob, old fd %d", fd, m_fd_array[fd].fd);
        return;
    }
    m_fd_array[fd].fd = fd;
    m_fd_array[fd].ioJob = ioJob;
    m_fd_array[fd].thrd_index = thrd_index;

    m_thrd_fds[thrd_index].push_back(fd);
    return;
}

int CIoJobMgr::walk_to_set_sets(fd_set *rset, fd_set *wset, fd_set *eset, int thrd_index)
{
    IOFD_LIST_Itr itr;
    int io_fd = 0;
    CIoJob *pIoJob = NULL;
    int maxFd = 0;

    assert(thrd_index >= 0 && thrd_index < MAX_THRD_CNT);

    for (itr = m_thrd_fds[thrd_index].begin();
            itr != m_thrd_fds[thrd_index].end(); )
    {
        io_fd = *itr;
        /*maybe timenode deleted in callback func, after that, itr not valid*/
        itr++;

        assert(io_fd >= 0 && io_fd < MAX_FD_CNT);

        if (m_fd_array[io_fd].thrd_index != thrd_index)
        {
            _LOG_ERROR("fd %d not in thrd %d when set fdsets, but %d", io_fd, thrd_index, m_fd_array[io_fd].thrd_index);
        }

        pIoJob = m_fd_array[io_fd].ioJob;
        if (NULL == pIoJob)
        {
            _LOG_ERROR("fd %d has no job when set fdsets", io_fd);
            continue;
        }
        
        if (io_fd > maxFd)
        {
            maxFd = io_fd;
        }

		//g_IoJobMgr->lock_fd(io_fd);

        if (pIoJob->io_event_read())
        {
            FD_SET(io_fd, rset);
            FD_SET(io_fd, eset);
        }
        if (pIoJob->io_event_write())
        {
            FD_SET(io_fd, wset);
            FD_SET(io_fd, eset);
        }

		//g_IoJobMgr->unlock_fd(io_fd);
#if 0
        else
        {
            _LOG_ERROR("fd event invalid when set.");
        }
#endif
    }

    return maxFd;
}

void CIoJobMgr::walk_to_handle_sets(fd_set *rset, fd_set *wset, fd_set *eset, int thrd_index)
{
    IOFD_LIST_Itr itr;
    int io_fd = 0;
    CIoJob *pIoJob = NULL;

    assert(thrd_index >= 0 && thrd_index < MAX_THRD_CNT);

    for (itr = m_thrd_fds[thrd_index].begin();
            itr != m_thrd_fds[thrd_index].end(); )
    {
        io_fd = *itr;
        /*maybe timenode deleted in callback func, after that, itr not valid*/
        itr++;

        assert(io_fd >= 0 && io_fd < MAX_FD_CNT);

        if (m_fd_array[io_fd].thrd_index != thrd_index)
        {
            _LOG_ERROR("fd %d not in thrd %d when set fdsets, but %d", io_fd, thrd_index, m_fd_array[io_fd].thrd_index);
        }

        pIoJob = m_fd_array[io_fd].ioJob;
        if (NULL == pIoJob)
        {
            _LOG_ERROR("fd %d has no job when set fdsets", io_fd);
            continue;
        }

        if (pIoJob->get_deleting_flag())
        {
            //if deleting, not handle it
            continue;
        }

        if (pIoJob->io_event_read())
        {
            if(FD_ISSET(io_fd, rset))
            {
                pIoJob->read_evt_handle();
            }

            if(FD_ISSET(io_fd, eset))
            {
                _LOG_WARN("fd %d execption evt comming when has read job", io_fd);
                pIoJob->read_evt_handle();
            }
        }
        
		if (pIoJob->io_event_write())
        {
            if(FD_ISSET(io_fd, wset))
            {
                pIoJob->write_evt_handle();
            }

            if(FD_ISSET(io_fd, eset))
            {
                _LOG_WARN("fd %d execption evt comming when has write job", io_fd);
                pIoJob->write_evt_handle();
            }
        }
    }
}

void CIoJobMgr::handle_deling_job(int thrd_index)
{
    IOFD_LIST_Itr itr;
    int io_fd = 0;
    CIoJob *pIoJob = NULL;

    assert(thrd_index >= 0 && thrd_index < MAX_THRD_CNT);
    
    for (itr = m_thrd_fds[thrd_index].begin();
            itr != m_thrd_fds[thrd_index].end(); )
    {
        io_fd = *itr;
        /*maybe timenode deleted in callback func, after that, itr not valid*/
        itr++;

        assert(io_fd >= 0 && io_fd < MAX_FD_CNT);

        if (m_fd_array[io_fd].thrd_index != thrd_index)
        {
            _LOG_ERROR("fd %d not in thrd %d when set fdsets, but %d", io_fd, thrd_index, m_fd_array[io_fd].thrd_index);
        }

        pIoJob = m_fd_array[io_fd].ioJob;
        if (NULL == pIoJob)
        {
            _LOG_ERROR("fd %d has no job when delete", io_fd);
            continue;
        }

        if (pIoJob->get_deleting_flag())
        {
			g_IoJobMgr->lock_fd(io_fd);

            m_thrd_fds[thrd_index].remove(io_fd);

            /*reinit data*/
            m_fd_array[io_fd].fd = -1;
            m_fd_array[io_fd].thrd_index = INVALID_THRD_INDEX;
            m_fd_array[io_fd].ioJob = NULL;

            /*free resource*/            
            pIoJob->free_callback();
            delete pIoJob;

			g_IoJobMgr->unlock_fd(io_fd);
        }
    }

    return;
}

BOOL CIoJobMgr::init()
{
    return true;
}

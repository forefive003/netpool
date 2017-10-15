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
#include "CThread.h"
#include "CThreadPool.h"
#include "CJobIo.h"
#include "CNetPoll.h"
#include "thrdComm.h"
#include "CThrdComObj.h"

int CThrdComObj::recv_handle(char *buf, int buf_len)
{
    if ((sizeof(m_recv_buf)-m_recv_len) < buf_len)
    {
    	_LOG_ERROR("already recv %d on thrd %d, now recv %d too long", m_recv_len, m_thrd_index, buf_len);
    	return -1;
    }

    memcpy(m_recv_buf+m_recv_len, buf, buf_len);
    m_recv_len += buf_len;

    if (m_recv_len < 8)
    {
    	_LOG_ERROR("recv %d on thrd %d, wait for data", m_recv_len, m_thrd_index);
    	return 0;
    }

    int thrd_index = *(int*)m_recv_buf;
    if (thrd_index != m_thrd_index)
    {
    	_LOG_ERROR("thrd index %d recv on thrd %d, not matched", thrd_index, m_thrd_index);
    	return -1;
    }

    int msg_type = *(int*)&m_recv_buf[4];
    BOOL ret = false;

    switch(msg_type)
    {
    	case MSG_DEL_IO_JOB:
    	{
    		if (m_recv_len < sizeof(MSG_DEL_IO_JOB_T))
    		{
    			return 0;
    		}
    		MSG_DEL_IO_JOB_T *delIoJob = NULL;
    		delIoJob = (MSG_DEL_IO_JOB_T*)&m_recv_buf[8];
    		ret = g_NetPoll->_del_io_job_entitiy(delIoJob->fd, delIoJob->free_func);
    		break;
    	}
		case MSG_ADD_LISTEN_JOB:
		{
			if (m_recv_len < sizeof(MSG_ADD_LISTEN_JOB_T))
    		{
    			return 0;
    		}
    		MSG_ADD_LISTEN_JOB_T *addListenIoJob = NULL;
    		addListenIoJob = (MSG_ADD_LISTEN_JOB_T*)&m_recv_buf[8];
    		ret = g_NetPoll->_add_listen_job_entitiy(addListenIoJob->acpt_func, 
    			addListenIoJob->fd, addListenIoJob->param1);
    		break;
    	}
		case MSG_DEL_LISTEN_JOB:
		{
			if (m_recv_len < sizeof(MSG_DEL_LISTEN_JOB_T))
    		{
    			return 0;
    		}
    		MSG_DEL_LISTEN_JOB_T *delListenIoJob = NULL;
    		delListenIoJob = (MSG_DEL_LISTEN_JOB_T*)&m_recv_buf[8];
    		ret = g_NetPoll->_del_listen_job_entitiy(addListenIoJob->fd, addListenIoJob->free_func);
    		break;
    	}
		case MSG_ADD_READ_JOB:
		{
			if (m_recv_len < sizeof(MSG_ADD_READ_JOB_T))
    		{
    			return 0;
    		}
    		MSG_ADD_READ_JOB_T *addReadIoJob = NULL;
    		addReadIoJob = (MSG_ADD_READ_JOB_T*)&m_recv_buf[8];
    		ret = g_NetPoll->_add_read_job_entitiy(addReadIoJob->read_func, 
			    			addReadIoJob->fd,
			    			addReadIoJob->param1,
			    			addReadIoJob->thrd_index,
			    			addReadIoJob->bufferSize,
			    			addReadIoJob->isTcp);
    		break;
    	}
		case MSG_DEL_READ_JOB:
		{
			if (m_recv_len < sizeof(MSG_DEL_READ_JOB_T))
    		{
    			return 0;
    		}
    		MSG_DEL_READ_JOB_T *delReadIoJob = NULL;
    		delReadIoJob = (MSG_DEL_READ_JOB_T*)&m_recv_buf[8];
    		ret = g_NetPoll->_del_read_job_entitiy(addReadIoJob->fd,
			    			addReadIoJob->free_func);
    		break;
    	}
		case MSG_ADD_WRITE_JOB:
		{
			if (m_recv_len < sizeof(MSG_ADD_WRITE_JOB_T))
    		{
    			return 0;
    		}

    		MSG_ADD_WRITE_JOB_T *addWriteIoJob = NULL;
    		addWriteIoJob = (MSG_ADD_WRITE_JOB_T*)&m_recv_buf[8];
    		ret = g_NetPoll->_add_write_job_entitiy(addWriteIoJob->io_func,
			    			addWriteIoJob->fd,
			    			addWriteIoJob->param1,
			    			addWriteIoJob->thrd_index,
			    			addWriteIoJob->isTcp);
    		break;
    	}
		case MSG_DEL_WRITE_JOB:
		{
			if (m_recv_len < sizeof(MSG_DEL_WRITE_JOB_T))
    		{
    			return 0;
    		}
    		MSG_DEL_WRITE_JOB_T *delWriteIoJob = NULL;
    		delWriteIoJob = (MSG_DEL_WRITE_JOB_T*)&m_recv_buf[8];
    		ret = g_NetPoll->_del_read_job_entitiy(delWriteIoJob->fd,
			    			delWriteIoJob->free_func);
    		break;
    	}
		case MSG_PAUSE_READ:
		{
			if (m_recv_len < sizeof(MSG_PAUSE_READ_T))
    		{
    			return 0;
    		}
    		MSG_PAUSE_READ_T *pauseReadIoJob = NULL;
    		pauseReadIoJob = (MSG_PAUSE_READ_T*)&m_recv_buf[8];
    		ret = g_NetPoll->_pause_read_job_entitiy(pauseReadIoJob->fd);
    		break;
    	}
		case MSG_RESUME_READ:
		{
			if (m_recv_len < sizeof(MSG_RESUME_READ_T))
    		{
    			return 0;
    		}
    		MSG_RESUME_READ_T *resumeReadIoJob = NULL;
    		resumeReadIoJob = (MSG_RESUME_READ_T*)&m_recv_buf[8];
    		ret = g_NetPoll->_resume_read_job_entitiy(resumeReadIoJob->fd);
    		break;
    	}
    	default:
    		_LOG_ERROR("invalid msg %d recv on thrd %d", msg_type, m_thrd_index);
    		return -1;
    }

    return ret?0:-1;
}

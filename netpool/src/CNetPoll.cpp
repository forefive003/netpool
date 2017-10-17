
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
#include "CJobIoMgr.h"
#include "CJobTime.h"
#include "CThreadPoolMgr.h"
#include "CThrdComServ.h"
#include "CNetPoll.h"

CNetPoll *g_NetPoll = NULL;

CNetPoll::CNetPoll() {
	m_cur_thrd_index = 0;
	m_isShutDown = false;

	m_cur_worker_thrds = 0;
	MUTEX_SETUP(m_lock);

	m_init_func = NULL;
	m_beat_func = NULL;
	m_exit_func = NULL;

	m_epfd = NULL;
	
	m_thrdMsgServ_array = NULL;

#ifdef _WIN32
    m_index_lock = 0;
#else
    pthread_spin_init(&m_index_lock, 0);
#endif
}

CNetPoll::~CNetPoll() {
	MUTEX_CLEANUP(m_lock);

#ifndef _WIN32
	if (NULL != m_epfd) 
	{
		for (unsigned int i = 0; i < g_ThreadPoolMgr->m_worker_thrd_cnt; i ++)
		{
			if (m_epfd[i] != -1)
			{
				close(m_epfd[i]);
				m_epfd[i] = -1;
			}
		}
	
		free(m_epfd);
	}
#endif
	
	if (NULL != m_thrdMsgServ_array) 
	{
		int cnt = 1;
		if (g_ThreadPoolMgr->m_worker_thrd_cnt > 1) 
		{
			cnt = g_ThreadPoolMgr->m_worker_thrd_cnt;
		}

		for (int i = 0; i < cnt; i++)
		{
			if (m_thrdMsgServ_array[i] != NULL)
			{
				delete m_thrdMsgServ_array[i];
			}
		}

		free(m_thrdMsgServ_array);
	}

#ifdef _WIN32
    m_index_lock = 0;
#else
    pthread_spin_destroy(&m_index_lock);
#endif

}

void CNetPoll::set_debug_func(thrd_init_func init_func,
						thrd_beat_func beat_func,
						thrd_exit_func exit_func)
{
	m_init_func = init_func;
	m_beat_func = beat_func;
	m_exit_func = exit_func;
}

#ifndef _WIN32
void CNetPoll::loop_handle(void *arg, void *param2, void *param3, void *param4)
{
	CNetPoll *pollObj = (CNetPoll*)arg;
	int thrd_index = (int)(long)param2;

#define EVENTMAX 1024
	struct epoll_event events[EVENTMAX];
	int fd_num = 0;
	int i = 0;

#if 0 /*why to black signal*/
	sigset_t block_set;
	sigemptyset(&block_set);
	sigfillset(&block_set);
	sigdelset(&block_set,SIGSEGV);
	sigdelset(&block_set,SIGBUS);
	sigdelset(&block_set,SIGPIPE);

	if( pthread_sigmask(SIG_BLOCK, &block_set,NULL) != 0 )
	{
		char err_buf[64] = {0};
		_LOG_ERROR("pthread_sigmask error %s\n", str_error_s(err_buf, sizeof(err_buf), errno));
		pthread_exit((void*)-1);
	}
#endif

	pollObj->m_thrdMsgServ_array[thrd_index] = new CThrdComServ(thrd_index, THRD_COMM_ADDR_STR, 0);
	if(0 != pollObj->m_thrdMsgServ_array[thrd_index]->init())
	{
		pthread_exit((void*)-1);
	}
	pollObj->m_thrdMsgServ_array[thrd_index]->set_thrd_tid(util_get_cur_tid());

	if (pollObj->m_init_func != NULL)
	{
		pollObj->m_init_func();
	}

	_LOG_INFO("work thread[%u] started.", thrd_index);

	MUTEX_LOCK(pollObj->m_lock);
	pollObj->m_cur_worker_thrds++;
	MUTEX_UNLOCK(pollObj->m_lock);

	g_TimeJobMgr->begin(thrd_index);

	while(false == pollObj->m_isShutDown)
	{
		bzero(events, sizeof(events));

		if (pollObj->m_beat_func != NULL)
		{
			pollObj->m_beat_func();
		}

		g_IoJobMgr->handle_deling_job(thrd_index);

		//MUTEX_LOCK(pollObj->m_ep_lock);
		fd_num = epoll_wait(pollObj->m_epfd[thrd_index], events, EVENTMAX, EPOLL_TIMEOUT*1000);
		//MUTEX_UNLOCK(pollObj->m_ep_lock);
		if (fd_num < 0)
		{
			char err_buf[64] = {0};

			if (EINTR == errno || errno == EAGAIN)
			{
				_LOG_DEBUG("select returned, continue, %s.",
						str_error_s(err_buf, sizeof(err_buf), errno));
				continue;
			}

			_LOG_INFO("epoll_wait failed, %s.",
									str_error_s(err_buf, sizeof(err_buf), errno));
			break;
		}
		else if (fd_num > 0)
		{
			for (i = 0; i < fd_num; i++)
			{
				if (events[i].events & EPOLLIN)
				{
					CIoJob *job_node = (CIoJob*)events[i].data.ptr;
					if (NULL == job_node)
					{
						_LOG_ERROR("EPOLLIN happen but no job node.");
					}
					else
					{
						job_node->read_evt_handle();
					}
				}
				/*有可能既有读事件又有写事件*/
				if (events[i].events & EPOLLOUT)
				{
					/*find write fd and write, del EPOLLOUT flag, otherwise loop*/
					CIoJob *job_node = (CWrIoJob*)events[i].data.ptr;
					if (NULL == job_node)
					{
						_LOG_ERROR("EPOLLOUT happen but no job node.");
					}
					else
					{
						job_node->write_evt_handle();
					}
				}
				if (events[i].events & EPOLLHUP)
				{
					/*app will feel it, let app to close*/
					CIoJob *job_node = (CIoJob*)events[i].data.ptr;
					if (NULL == job_node)
					{
						_LOG_ERROR("EPOLLHUP happen but no job node.");
					}
					else
					{
						_LOG_WARN("fd %d get EPOLLHUP, err %s.", job_node->get_fd(), strerror(errno));
					}
				}
				//else
				//{
				//	_LOG_ERROR("epoll_wait unexpect event 0x%x.", events[i].events);
				//}
			}
		}

		/*do timeout jobs*/
		g_TimeJobMgr->run(thrd_index);
	}

	g_IoJobMgr->handle_deling_job(thrd_index);

	if (pollObj->m_exit_func != NULL)
	{
		pollObj->m_exit_func();
	}

	MUTEX_LOCK(pollObj->m_lock);
	pollObj->m_cur_worker_thrds--;
	MUTEX_UNLOCK(pollObj->m_lock);

	_LOG_INFO("work thread[%u] exited.", thrd_index);
}
#else
void CNetPoll::loop_handle(void *arg, void *param2, void *param3, void *param4)
{
	CNetPoll *pollObj = (CNetPoll*)arg;
	int thrd_index = (int)(long)param2;
	///TODO
	int fd_num = 0;
	int i = 0;

	pollObj->m_thrdMsgServ_array[thrd_index] = new CThrdComServ(thrd_index, THRD_COMM_ADDR_STR, 0);
	if(0 != pollObj->m_thrdMsgServ_array[thrd_index]->init())
	{
		pthread_exit((void*)-1);
	}
	pollObj->m_thrdMsgServ_array[thrd_index]->set_thrd_tid(util_get_cur_tid());

	if (pollObj->m_init_func != NULL)
	{
		pollObj->m_init_func();
	}

	_LOG_INFO("work thread[%u] started.", thrd_index);

	MUTEX_LOCK(pollObj->m_lock);
	pollObj->m_cur_worker_thrds++;
	MUTEX_UNLOCK(pollObj->m_lock);

	g_TimeJobMgr->begin(thrd_index);

	SOCKET hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	while(false == pollObj->m_isShutDown)
	{
		fd_set rset;
		fd_set wset;
		fd_set eset;
		FD_ZERO(&rset);
		FD_ZERO(&wset);
		FD_ZERO(&eset);

		if (pollObj->m_beat_func != NULL)
		{
			pollObj->m_beat_func();
		}

		g_IoJobMgr->handle_deling_job(thrd_index);

		int maxFd = 0;
		maxFd = g_IoJobMgr->walk_to_set_sets(&rset, &wset, &eset);
		//windows select must has one fd
		if (maxFd == 0)
		{
			FD_SET(hSocket, &rset);
			maxFd = (int)hSocket;
		}

		struct timeval tv;
		tv.tv_sec = EPOLL_TIMEOUT;
		tv.tv_usec = 0;
		fd_num = select(maxFd + 1, &rset, &wset, &eset, &tv);
		if (fd_num < 0)
		{
			DWORD dwError = WSAGetLastError();
			if (EINTR == dwError || dwError == EAGAIN)
			{
				_LOG_INFO("select returned, continue, %d.", dwError);
				continue;
			}

			_LOG_INFO("epoll_wait failed, %d.", dwError);
			break;
		}
		else if (fd_num > 0)
		{
			g_IoJobMgr->walk_to_handle_sets(&rset, &wset, &eset);
		}

		/*do timeout jobs*/
		g_TimeJobMgr->run(thrd_index);
	}

	closesocket(hSocket);

	g_IoJobMgr->handle_deling_job(thrd_index);

	if (pollObj->m_exit_func != NULL)
	{
		pollObj->m_exit_func();
	}

	MUTEX_LOCK(pollObj->m_lock);
	pollObj->m_cur_worker_thrds--;
	MUTEX_UNLOCK(pollObj->m_lock);

	_LOG_INFO("work thread[%u] exited.", thrd_index);
}
#endif

void CNetPoll::pause_io_writing_evt(int thrd_index, CIoJob *jobNode)
{
	if (jobNode->io_event_write() == false)
	{
		return;
	}
#ifndef _WIN32
	char err_buf[64] = {0};
	if (jobNode->io_event_read())
	{
		/*modify*/
		struct epoll_event ev;

		memset(&ev, 0, sizeof(ev));
		ev.events = EPOLLIN | EPOLLET;
		ev.data.ptr = (void*)jobNode;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_MOD, jobNode->get_fd(), &ev) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_MOD failed, %s.", str_error_s(err_buf, sizeof(err_buf), errno));
		}
		else
		{
			_LOG_DEBUG("pause write event from io job, fd %d on thrd %d.", jobNode->get_fd(), thrd_index);
		}
	}
	else
	{
		/*del*/
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_DEL, jobNode->get_fd(), NULL) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_DEL failed, %s.", str_error_s(err_buf, sizeof(err_buf), errno));
		}
		else
		{
			_LOG_DEBUG("del write event from io job, fd %d on thrd %d.", jobNode->get_fd(), thrd_index);
		}
	}
#endif

	jobNode->del_write_io_event();
}

void CNetPoll::pause_io_reading_evt(int thrd_index, CIoJob *jobNode)
{
	if (jobNode->io_event_read() == false)
	{
		return;
	}
#ifndef _WIN32
	char err_buf[64] = {0};
	if (jobNode->io_event_write())
	{
		/*modify*/
		struct epoll_event ev;

		memset(&ev, 0, sizeof(ev));
		ev.events = EPOLLOUT | EPOLLET;
		ev.data.ptr = (void*)jobNode;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_MOD, jobNode->get_fd(), &ev) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_MOD failed, %s.", str_error_s(err_buf, sizeof(err_buf), errno));
		}
		else
		{
			_LOG_DEBUG("pause read event from io job, fd %d on thrd %d.", jobNode->get_fd(), thrd_index);
		}
	}
	else
	{
		/*del*/
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_DEL, jobNode->get_fd(), NULL) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_DEL failed, %s.", str_error_s(err_buf, sizeof(err_buf), errno));
		}
		else
		{
			_LOG_DEBUG("pause read event from io job, fd %d on thrd %d, no event now.", 
				jobNode->get_fd(), thrd_index);
		}
	}
#endif

	jobNode->del_read_io_event();
}

BOOL CNetPoll::_pause_io_reading_evt_entity(int fd, int thrd_index)
{
	CIoJob *job_node = NULL;

	job_node = g_IoJobMgr->get_fd_io_job(thrd_index, fd);
	if (NULL == job_node)
	{	
		_LOG_ERROR("io job fd %d not exist when pause read.", fd);
		return false;
	}
	this->pause_io_reading_evt(thrd_index, job_node);
	return true;
}

BOOL CNetPoll::_resume_io_reading_evt_entity(int fd, int thrd_index)
{
	char err_buf[64] = {0};
	CIoJob *job_node = NULL;

	job_node = g_IoJobMgr->get_fd_io_job(thrd_index, fd);
	if (NULL == job_node)
	{	
		_LOG_ERROR("io job fd %d not exist when resume read.", fd);
		return false;
	}

	if (job_node->io_event_write())
	{
#ifndef _WIN32
		struct epoll_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.data.ptr = (void*)job_node;
		ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_MOD, fd, &ev) != 0)
		{
			_LOG_ERROR("epoll_ctl mod to read failed, fd %d, epfd %d, errno %d, %s.",
								fd,	m_epfd[thrd_index],
								errno, str_error_s(err_buf, sizeof(err_buf), errno));
			return false;
		}
#endif
		_LOG_INFO("modify job, resume read to write job, fd %d on thrd %d.", fd, thrd_index);
	}
	/*加个判断,避免重复add read job*/
	else if (!job_node->io_event_read())
	{
#ifndef _WIN32
		struct epoll_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.data.ptr = (void*)job_node;
		ev.events = EPOLLIN | EPOLLET;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_ADD, fd, &ev) != 0)
		{
			_LOG_ERROR("epoll_ctl resume read failed, fd %d, %s.", fd,
					 str_error_s(err_buf, sizeof(err_buf), errno));
			return false;
		}	
#endif

		_LOG_INFO("modify job, resume read event, fd %d on thrd %d.", fd, thrd_index);
	}
	else
	{
		_LOG_WARN("already has read when resume read event, fd %d on thrd %d.", fd, thrd_index);
	}
	
	job_node->add_read_io_event();
	return true;
}

BOOL CNetPoll::_add_listen_job_entity(accept_hdl_func io_func, 
	int  fd, void* param1, 
	int thrd_index)
{
	char err_buf[64] = {0};
	
	if (NULL != g_IoJobMgr->get_fd_io_job(thrd_index, fd))
	{
		_LOG_ERROR("fd %d already has a listen job when add", fd);
		return false;
	}

	CListenJob *job_node = new CListenJob(fd, param1);
	assert(job_node);
	job_node->set_read_callback((void*)io_func);
	job_node->add_read_io_event();

#ifndef _WIN32
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN | EPOLLET;
	ev.data.ptr = (void*)job_node;
	//for (unsigned int i = 0; i < g_ThreadPoolMgr->m_worker_thrd_cnt; i++)
	{
		if(epoll_ctl(m_epfd[0], EPOLL_CTL_ADD, fd, &ev) != 0)
		{
			delete job_node;
			_LOG_ERROR("epoll_ctl add listen failed, fd %d, %s.", fd,
					str_error_s(err_buf, sizeof(err_buf), errno));
			assert(0);
			return false;
		}
	}	
#endif

	g_IoJobMgr->add_io_job(fd, thrd_index, job_node);
	_LOG_INFO("add new listen job on thrd %d, fd %d.", thrd_index, fd);
	return true;
}

BOOL CNetPoll::_del_listen_job_entity(int fd, free_hdl_func free_func, int thrd_index)
{
	char err_buf[64] = {0};

	CIoJob *job_node = NULL;

	job_node = g_IoJobMgr->get_fd_io_job(thrd_index, fd);
	if (NULL == job_node)
	{
		_LOG_ERROR("listen job fd %d not exist when del", fd);
		return false;
	}
	job_node->set_free_callback((void*)free_func);
	job_node->set_deleting_flag();

#ifndef _WIN32
	/*delete from epoll fd*/
	//for (unsigned int i = 0; i < g_ThreadPoolMgr->m_worker_thrd_cnt; i++)
	{
		if(epoll_ctl(m_epfd[0], EPOLL_CTL_DEL, fd, NULL) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_DEL failed, %s.", str_error_s(err_buf, sizeof(err_buf), errno));
			return false;
		}
	}
#endif

	_LOG_INFO("del listen job, fd %d on thrd %d", fd, thrd_index);
	return true;
}

BOOL CNetPoll::_add_read_job_entity(read_hdl_func io_func,
						int  fd, void* param1,
						int thrd_index,
						int bufferSize, BOOL isTcp)
{
	char err_buf[64] = {0};

	CIoJob *job_node = NULL;

	job_node = g_IoJobMgr->get_fd_io_job(thrd_index, fd);
	if (NULL == job_node)
	{
		if (isTcp)
		{
			job_node = new CTcpJob(fd, param1);
		}
		else
		{
			job_node = new CUdpJob(fd, param1);
		}

		assert(job_node);
		job_node->set_read_callback((void*)io_func);
		job_node->add_read_io_event();
		job_node->init_recv_buf(bufferSize);
#ifndef _WIN32
		struct epoll_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.events = EPOLLIN | EPOLLET;
		ev.data.ptr = (void*)job_node;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_ADD, fd, &ev) != 0)
		{
			delete job_node;
			_LOG_ERROR("epoll_ctl add read failed, fd %d, %s.", fd,
					 str_error_s(err_buf, sizeof(err_buf), errno));
			return false;
		}
#endif
		g_IoJobMgr->add_io_job(fd, thrd_index, job_node);
		_LOG_INFO("add new read job, fd %d on thrd %d.", fd, thrd_index);
		return true;
	}

	if (job_node->io_event_write())
	{
#ifndef _WIN32
		struct epoll_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.data.ptr = (void*)job_node;
		ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_MOD, fd, &ev) != 0)
		{
			_LOG_ERROR("epoll_ctl mod to read failed, fd %d, epfd %d, errno %d, %s.",
								fd,	m_epfd[thrd_index],
								errno, str_error_s(err_buf, sizeof(err_buf), errno));
			return false;
		}
#endif
		_LOG_INFO("modify job, add read to write job, fd %d on thrd %d.", fd, thrd_index);
	}
	/*加个判断,避免重复add read job*/
	else if (!job_node->io_event_read())
	{
#ifndef _WIN32
		struct epoll_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.data.ptr = (void*)job_node;
		ev.events = EPOLLIN | EPOLLET;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_ADD, fd, &ev) != 0)
		{
			_LOG_ERROR("epoll_ctl add read failed, fd %d, %s.", fd,
					 str_error_s(err_buf, sizeof(err_buf), errno));
			return false;
		}	
#endif

		_LOG_INFO("modify job, add read event, fd %d on thrd %d.", fd, thrd_index);
	}
	
	job_node->set_read_callback((void*)io_func);
	job_node->add_read_io_event();
	job_node->init_recv_buf(bufferSize);
	return true;
}

BOOL CNetPoll::_del_read_job_entity(int fd, free_hdl_func free_func, int thrd_index)
{
	char err_buf[64] = {0};

	CIoJob *job_node = NULL;

	job_node = g_IoJobMgr->get_fd_io_job(thrd_index, fd);
	if (NULL == job_node)
	{
		_LOG_ERROR("read job fd %d not exist.", fd);
		return false;
	}

	job_node->set_free_callback((void*)free_func);

	if (job_node->io_event_write())
	{
#ifndef _WIN32
		/*modify*/
		struct epoll_event ev;

		memset(&ev, 0, sizeof(ev));
		ev.events = EPOLLOUT | EPOLLET;
		ev.data.ptr = (void*)job_node;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_MOD, fd, &ev) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_MOD failed, %s.", str_error_s(err_buf, sizeof(err_buf), errno));
		}
#endif
		_LOG_INFO("del read event and set to write, fd %d on thrd %d", fd, thrd_index);
	}
	else if (job_node->io_event_read())
	{
#ifndef _WIN32
		/*delete from epoll fd*/
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_DEL, fd, NULL) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_DEL failed, %s.", str_error_s(err_buf, sizeof(err_buf), errno));
		}
#endif
		_LOG_INFO("del read event and del job, fd %d on thrd %d", fd, thrd_index);
		job_node->set_deleting_flag();
	}
	else
	{
		/*有可能job上什么事件都没有*/
		job_node->set_deleting_flag();
		_LOG_INFO("del read job, fd %d on thrd %d", fd, thrd_index);
	}

	job_node->del_read_io_event();
	return true;
}

BOOL CNetPoll::_add_write_job_entity(write_hdl_func io_func,
						int  fd, void* param1, 
						int thrd_index,
						BOOL isTcp)
{
	char err_buf[64] = {0};

	CIoJob *job_node = NULL;

	job_node = g_IoJobMgr->get_fd_io_job(thrd_index, fd);
	if (NULL == job_node)
	{
		if (isTcp)
		{
			job_node = new CTcpJob(fd, param1);
		}
		else
		{
			job_node = new CUdpJob(fd, param1);
		}

		assert(job_node);
		job_node->set_write_callback((void*)io_func);
		job_node->add_write_io_event();

#ifndef _WIN32
		struct epoll_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.events = EPOLLOUT | EPOLLET;
		ev.data.ptr = (void*)job_node;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_ADD, fd, &ev) != 0)
		{
			delete job_node;
			_LOG_ERROR("epoll_ctl add write failed, fd %d, %s.", fd,
					str_error_s(err_buf, sizeof(err_buf), errno));
			return false;
		}
#endif
		g_IoJobMgr->add_io_job(fd, thrd_index, job_node);
		_LOG_INFO("add new write job, fd %d on thrd %d.", fd, thrd_index);
		return true;
	}

	job_node->set_write_callback((void*)io_func);

	if (false == job_node->io_event_write())
	{
		_LOG_DEBUG("modify job, add write event, fd %d on thrd %d.", fd, thrd_index);
		job_node->add_write_io_event();

#ifndef _WIN32		
		struct epoll_event ev;
		memset(&ev, 0, sizeof(ev));
		ev.data.ptr = (void*)job_node;
		ev.events = EPOLLOUT;
		if (job_node->io_event_read())
		{
			ev.events |= EPOLLIN | EPOLLET;
			if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_MOD, fd, &ev) != 0)
			{
				_LOG_ERROR("epoll_ctl mod to write failed, fd %d, %s.",
									fd,	str_error_s(err_buf, sizeof(err_buf), errno));
				return false;
			}
		}
		else
		{
			if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_ADD, fd, &ev) != 0)
			{
				_LOG_ERROR("epoll_ctl add write failed, fd %d, %s.", fd,
						str_error_s(err_buf, sizeof(err_buf), errno));
				return false;
			}
		}
#endif
	}
	
	return true;
}


BOOL CNetPoll::_del_write_job_entity(int fd, free_hdl_func free_func, int thrd_index)
{
	char err_buf[64] = {0};

	CIoJob *job_node = NULL;

	job_node = g_IoJobMgr->get_fd_io_job(thrd_index, fd);
	if (NULL == job_node)
	{
		/*对于write事件, 在处理时会自动停止,因此如果del_read_job会直接删除,
		程序再调用此接口时job已经不存在,属于正常流程*/
		//_LOG_DEBUG("write job fd %d not exist when del.", fd);
		return true;
	}
	job_node->set_free_callback((void*)free_func);

	if (job_node->io_event_read())
	{
#ifndef _WIN32
		/*modify*/
		struct epoll_event ev;

		memset(&ev, 0, sizeof(ev));
		ev.events = EPOLLIN | EPOLLET;
		ev.data.ptr = (void*)job_node;
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_MOD, fd, &ev) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_MOD failed, epfd %d, %s.",
				m_epfd[thrd_index],
				str_error_s(err_buf, sizeof(err_buf), errno));
		}
#endif
		_LOG_INFO("del write event and set to read, fd %d on thrd %d", fd, thrd_index);
	}
	else if (job_node->io_event_write())
	{
#ifndef _WIN32
		/*delete from epoll fd*/
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_DEL, fd, NULL) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_DEL failed, epfd %d, %s.", 
				m_epfd[thrd_index],
				str_error_s(err_buf, sizeof(err_buf), errno));
		}

#endif
		_LOG_INFO("del write event and del job, fd %d on thrd %d", fd, thrd_index);

		job_node->set_deleting_flag();
	}
	else
	{
		job_node->set_deleting_flag();
		_LOG_INFO("del write job, fd %d on thrd %d", fd, thrd_index);
	}

	job_node->del_write_io_event();
	return true;
}

BOOL CNetPoll::_del_io_job_entity(int fd, free_hdl_func free_func, int thrd_index)
{
	char err_buf[64] = {0};

	CIoJob *job_node = NULL;

	job_node = g_IoJobMgr->get_fd_io_job(thrd_index, fd);
	if (NULL == job_node)
	{
		/*对于write事件, 在处理时会自动停止,因此如果del_read_job会直接删除,
		程序再调用此接口时job已经不存在,属于正常流程*/
		//_LOG_DEBUG("write job fd %d not exist when del.", fd);
		return false;
	}
	job_node->set_free_callback((void*)free_func);
	if (job_node->io_event_read() || job_node->io_event_write())
	{
		job_node->del_write_io_event();
		job_node->del_read_io_event();
#ifndef _WIN32
		/*delete from epoll fd*/
		if(epoll_ctl(m_epfd[thrd_index], EPOLL_CTL_DEL, fd, NULL) != 0)
		{
			_LOG_ERROR("EPOLL_CTL_DEL failed, epfd %d, %s.", 
				m_epfd[thrd_index],
				str_error_s(err_buf, sizeof(err_buf), errno));
		}

#endif
		_LOG_INFO("del io job, fd %d on thrd %d", fd, thrd_index);
		job_node->set_deleting_flag();
	}
	else
	{
		_LOG_INFO("del io job, fd %d on thrd %d, no event on it.", fd, thrd_index);
		job_node->set_deleting_flag();		
	}

	return true;
}

BOOL CNetPoll::add_listen_job(accept_hdl_func acpt_func,
								int fd, void* param1)
{
	int thrd_index = get_next_thrd_index();
	UTIL_TID tid = util_get_cur_tid();
	if (m_thrdMsgServ_array[thrd_index]->get_thrd_tid() == tid)
	{
		return this->_add_listen_job_entity(acpt_func, fd, param1, thrd_index);
	}

	MSG_ADD_LISTEN_JOB_T msgAddListenJob;
	msgAddListenJob.acpt_func = acpt_func;
	msgAddListenJob.fd 		= fd;
	msgAddListenJob.param1	= param1;
	msgAddListenJob.thrd_index = thrd_index;
	if(0 != m_thrdMsgServ_array[thrd_index]->send_comm_msg(MSG_ADD_LISTEN_JOB, 
			(char*)&msgAddListenJob, sizeof(msgAddListenJob)))
	{
		return false;
	}
	return true;
}
BOOL CNetPoll::del_listen_job(int fd, free_hdl_func free_func)
{
	int thrd_index = g_IoJobMgr->get_fd_thrd_index(fd);
	UTIL_TID tid = util_get_cur_tid();
	if (m_thrdMsgServ_array[thrd_index]->get_thrd_tid() == tid)
	{
		return this->_del_listen_job_entity(fd, free_func, thrd_index);
	}

	MSG_DEL_LISTEN_JOB_T msgDelListenJob;
	msgDelListenJob.fd = fd;
	msgDelListenJob.free_func = free_func;
	if(0 != m_thrdMsgServ_array[thrd_index]->send_comm_msg(MSG_DEL_LISTEN_JOB, 
			(char*)&msgDelListenJob, sizeof(msgDelListenJob)))
	{
		return false;
	}
	return true;
}

BOOL CNetPoll::add_read_job(read_hdl_func read_func,
				int fd, 
				void* param1,
				int thrd_index,
				int bufferSize,
				BOOL isTcp)
{
	if (thrd_index >= (int)g_ThreadPoolMgr->m_worker_thrd_cnt)
	{
		thrd_index = get_next_thrd_index();
	}

	UTIL_TID tid = util_get_cur_tid();
	if (m_thrdMsgServ_array[thrd_index]->get_thrd_tid() == tid)
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
	if(0 != m_thrdMsgServ_array[thrd_index]->send_comm_msg(MSG_ADD_READ_JOB, 
			(char*)&msgAddReadJob, sizeof(msgAddReadJob)))
	{
		return false;
	}
	return true;
}

BOOL CNetPoll::del_read_job(int  fd, free_hdl_func free_func)
{
	int thrd_index = g_IoJobMgr->get_fd_thrd_index(fd);
	UTIL_TID tid = util_get_cur_tid();
	if (m_thrdMsgServ_array[thrd_index]->get_thrd_tid() == tid)
	{
		return this->_del_read_job_entity(fd, free_func, thrd_index);
	}

	MSG_DEL_READ_JOB_T msgDelReadJob;
	msgDelReadJob.fd = fd;
	msgDelReadJob.free_func = free_func;
	if(0 != m_thrdMsgServ_array[thrd_index]->send_comm_msg(MSG_DEL_READ_JOB, 
			(char*)&msgDelReadJob, sizeof(msgDelReadJob)))
	{
		return false;
	}
	return true;		
}

BOOL CNetPoll::add_write_job(write_hdl_func io_func,
					int  fd, 
					void* param1, 
					int thrd_index,
					BOOL isTcp)
{
	if (thrd_index >= (int)g_ThreadPoolMgr->m_worker_thrd_cnt)
	{
		thrd_index = get_next_thrd_index();
	}

	UTIL_TID tid = util_get_cur_tid();
	if (m_thrdMsgServ_array[thrd_index]->get_thrd_tid() == tid)
	{
		return this->_add_write_job_entity(io_func, fd, param1, thrd_index, isTcp);
	}

	MSG_ADD_WRITE_JOB_T msgAddWriteJob;
	msgAddWriteJob.io_func = io_func;
	msgAddWriteJob.fd 		= fd;
	msgAddWriteJob.param1	= param1;
	msgAddWriteJob.thrd_index = thrd_index;
	msgAddWriteJob.isTcp 	 = isTcp;
	if(0 != m_thrdMsgServ_array[thrd_index]->send_comm_msg(MSG_ADD_WRITE_JOB, 
			(char*)&msgAddWriteJob, sizeof(msgAddWriteJob)))
	{
		return false;
	}
	return true;
}
BOOL CNetPoll::del_write_job(int  fd, free_hdl_func free_func)
{
	int thrd_index = g_IoJobMgr->get_fd_thrd_index(fd);
	UTIL_TID tid = util_get_cur_tid();
	if (m_thrdMsgServ_array[thrd_index]->get_thrd_tid() == tid)
	{
		return this->_del_write_job_entity(fd, free_func, thrd_index);
	}

	MSG_DEL_WRITE_JOB_T msgDelWriteJob;
	msgDelWriteJob.fd = fd;
	msgDelWriteJob.free_func = free_func;
	if(0 != m_thrdMsgServ_array[thrd_index]->send_comm_msg(MSG_DEL_WRITE_JOB, 
			(char*)&msgDelWriteJob, sizeof(msgDelWriteJob)))
	{
		return false;
	}
	return true;		
}

BOOL CNetPoll::pause_io_reading_evt(int fd)
{
	int thrd_index = g_IoJobMgr->get_fd_thrd_index(fd);
	UTIL_TID tid = util_get_cur_tid();
	if (m_thrdMsgServ_array[thrd_index]->get_thrd_tid() == tid)
	{
		return this->_pause_io_reading_evt_entity(fd, thrd_index);
	}

	MSG_PAUSE_READ_T msgPauseRead;
	msgPauseRead.fd = fd;
	if(0 != m_thrdMsgServ_array[thrd_index]->send_comm_msg(MSG_PAUSE_READ, 
			(char*)&msgPauseRead, sizeof(msgPauseRead)))
	{
		return false;
	}
	return true;
}
BOOL CNetPoll::resume_io_reading_evt(int fd)
{
	int thrd_index = g_IoJobMgr->get_fd_thrd_index(fd);
	UTIL_TID tid = util_get_cur_tid();
	if (m_thrdMsgServ_array[thrd_index]->get_thrd_tid() == tid)
	{
		return this->_resume_io_reading_evt_entity(fd, thrd_index);
	}

	MSG_RESUME_READ_T msgResumeRead;
	msgResumeRead.fd = fd;
	if(0 != m_thrdMsgServ_array[thrd_index]->send_comm_msg(MSG_RESUME_READ, 
			(char*)&msgResumeRead, sizeof(msgResumeRead)))
	{
		return false;
	}
	return true;
}

BOOL CNetPoll::del_io_job(int fd, free_hdl_func free_func)
{
	int thrd_index = g_IoJobMgr->get_fd_thrd_index(fd);
	UTIL_TID tid = util_get_cur_tid();
	if (m_thrdMsgServ_array[thrd_index]->get_thrd_tid() == tid)
	{
		return this->_del_io_job_entity(fd, free_func, thrd_index);
	}

	MSG_DEL_IO_JOB_T msgDelIoJob;
	msgDelIoJob.fd = fd;
	msgDelIoJob.free_func = free_func;
	if(0 != m_thrdMsgServ_array[thrd_index]->send_comm_msg(MSG_DEL_IO_JOB, 
			(char*)&msgDelIoJob, sizeof(msgDelIoJob)))
	{
		return false;
	}
	return true;
}

BOOL CNetPoll::init()
{
	m_thrdMsgServ_array = (CThrdComServ**)malloc(sizeof(CThrdComServ*) * g_ThreadPoolMgr->m_worker_thrd_cnt);
	memset(m_thrdMsgServ_array, 0, sizeof(CThrdComServ*) * g_ThreadPoolMgr->m_worker_thrd_cnt);

#ifndef _WIN32
	if (g_ThreadPoolMgr->m_worker_thrd_cnt > 0)
	{
		m_epfd = (int*)malloc(sizeof(int) * g_ThreadPoolMgr->m_worker_thrd_cnt);

		for (unsigned int i = 0; i < g_ThreadPoolMgr->m_worker_thrd_cnt; i++)
		{
			m_epfd[i] = epoll_create(1);
			if(m_epfd[i] == -1)
			{
				_LOG_ERROR("epoll_create failed.");
				assert(0);
			}			
		}
	}
	else
	{
		m_epfd = (int*)malloc(sizeof(int));

		m_epfd[0] = epoll_create(1);
		if(m_epfd[0] == -1)
		{
			_LOG_ERROR("epoll_create failed.");
			assert(0);
		}
	}
#endif
	return TRUE;
}

unsigned int CNetPoll::get_next_thrd_index()
{
	int ret = 0;
#ifdef _WIN32
    while (InterlockedExchange(&m_index_lock, 1) == 1){
        sleep_s(0);
    }
#else
    pthread_spin_lock(&m_index_lock);
#endif
	ret = m_cur_thrd_index;

	m_cur_thrd_index++;
	if (m_cur_thrd_index >= g_ThreadPoolMgr->m_worker_thrd_cnt)
	{
		m_cur_thrd_index = 0;
	}
#ifdef _WIN32
    InterlockedExchange(&m_index_lock, 0);
#else
    pthread_spin_unlock(&m_index_lock);
#endif
	return ret;
}

BOOL CNetPoll::start()
{
	m_isShutDown = false;

	if (NULL != g_ThreadPoolMgr->m_worker_thrd_pool)
	{
		for (unsigned int i = 0; i < g_ThreadPoolMgr->m_worker_thrd_cnt; i++)
		{
			CEvtTask *listenTask = new CEvtTask((evt_hdl_func)loop_handle,
					(void*)this, (void*)(long)i, NULL, NULL);
			g_ThreadPoolMgr->m_worker_thrd_pool->Execute(listenTask);
		}
	}
	else
	{
		loop_handle((void*)this, (void*)(long)0, NULL, NULL);
	}

	return true;
}

void CNetPoll::let_stop()
{
	/*set to shutdown all thread*/
	m_isShutDown = true;
	/*maybe call this function in signal func, which easy to cause dead lock*/
	//_LOG_INFO("let loop to exit.");
}

void CNetPoll::wait_stop()
{
	/*wait*/
	int loopCnt = 0;
	while(m_cur_worker_thrds != 0)
	{
#ifdef _WIN32
		Sleep(1);
#else
		usleep(100);
#endif
		loopCnt++;
		if (0 == (loopCnt % 60000))
		{
			_LOG_WARN("wait all loop exit, loop %d, cur %d.",
					loopCnt, m_cur_worker_thrds);
		}
	}

	_LOG_INFO("all loop exit succ.");
}

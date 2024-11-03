#pragma once
#include "Epoll.h"
#include "Thread.h"
#include "Function.h"
#include "Socket.h"

class CThreadPool
{
public:
	CThreadPool() {
		m_server = nullptr;
		timespec tp = { 0,0 };
		clock_gettime(CLOCK_REALTIME, &tp);
		char* buf = NULL;
		asprintf(&buf, "%d.%d.sock", tp.tv_sec % 100000, tp.tv_nsec % 1000000);
		if (buf) {
			m_path = buf;
			free(buf);
		}//有问题的话，在start接口里面判断m_path来解决问题
		usleep(1);
	}
	~CThreadPool() {
		Close();
	}
	CThreadPool(const CThreadPool&)=delete;
	CThreadPool& operator=(const CThreadPool&)=delete;
public:
	int Start(unsigned nThreads) {
		int ret = 0;
		if(m_server)return -1;//已经初始化了
		if (m_path.size() == 0)return -2;//构造函数失败
		m_server = new CSocket();
		if (m_server == nullptr)return -3;
		ret = m_server->Init(CSockParam(m_path, SOCK_ISSERVER));
		if (ret != 0)return -4;
		ret = m_epoll.Create(nThreads);
		if (ret != 0)return -5;
		ret = m_epoll.Add(*m_server, EpollData((void*)m_server));
		if (ret != 0)return -6;
		m_threads.resize(nThreads);
		for (unsigned i = 0; i < nThreads; i++) {
			m_threads[i] = new CThread(&CThreadPool::TaskDispatch, this);
			if (m_threads[i] == nullptr)return -7;
			ret = m_threads[i]->Start();
			if(ret!= 0)return -8;
		}
		return 0;
	}
	void Close() {
		m_epoll.Close();
		if (m_server) {
			CSocketBase* p = m_server;
			m_server = nullptr;
			delete p;
		}
		for (auto& t : m_threads) {
			if (t) {
				CThread* p = t;
				t = nullptr;
				delete p;
			}
		}
		m_threads.clear();
		unlink(m_path);
	}
	template<typename _FUNCTION_, typename... _ARGS_>
	int AddTask(_FUNCTION_ func, _ARGS_... args) {
		static thread_local CSocket client;
		int ret = 0;
		if (client == -1) {
			ret = client.Init(CSockParam(m_path, 0));
			if (ret!= 0)return -1;
			ret = client.Link();
			if (ret!= 0)return -2;
		}
		CFunctionBase* base = new CFunction<_FUNCTION_, _ARGS_...>(func, args...);
		if (base == nullptr)return -3;
		Buffer data(sizeof(base));
		memcpy(data, &base, sizeof(base));
		ret = client.Send(data);
		if (ret != 0) {
			delete base;
			return -4;
		}
		return 0;
	}
	size_t Size()const { return m_threads.size(); }
private:
	int TaskDispatch() {
		while (m_epoll != -1) {
			EPEvents events;
			int ret = 0;
			ssize_t esize = m_epoll.WaitEvents(events);
			if (esize > 0) {
				for (ssize_t i = 0; i < esize; i++) {
					CSocketBase* pClient = nullptr;
					if (events[i].events & EPOLLIN) {
						if (events[i].data.ptr == m_server) {//客户端请求连接
							ret = m_server->Link(&pClient);
							if (ret != 0)continue;
							ret = m_epoll.Add(*pClient, EpollData((void*)pClient));
							if (ret != 0) {
								delete pClient;
								continue;
							}
						}
						else {//客户端的数据来了
							pClient = (CSocketBase*)events[i].data.ptr;
							if (pClient) {
								CFunctionBase* base = nullptr;
								Buffer data(sizeof(base));
								ret = pClient->Recv(data);
								if (ret <= 0) {
									m_epoll.Del(*pClient);
									delete pClient;
									continue;
								}
								memcpy(&base, (char*)data, sizeof(base));
								if (base != nullptr) {
									(*base)();
									delete base;
								}
							}
						}
					}
				}
			}
		}
		return 0;
	}
private:
	CEpoll m_epoll;
	std::vector<CThread*> m_threads;
	CSocketBase* m_server;
	Buffer m_path;
};
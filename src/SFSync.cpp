#pragma warning (disable:4819)

#include "SFSync.h"
#include "SDL.h"
#include "SFMutex.h"
#include <string>
#include <vector>
#include <list>

namespace pioneer
{
	static long long __errorCode = 0;
	static std::string __errorMsg = "";
	static SFMutex __mutex;

	enum SFState
	{
		kStatePoll = 0,
		kStateLoop = 1,
		kStateTerm = 2,
	};

	struct SFThread
	{
		SFSync* _sync;
		SDL_Thread* _thread;
		std::string _name;
		SFFunc _func;
		void* _param;
		SFState _state;
	};

	class SFSync::SFSyncImpl
	{
	public:
		std::vector<SFThread> _threads;
		std::list<SFMsg> _msgs;
		SFState _state;
	};

	SFSync::SFSync()
	{
		_impl = NULL;
	}
	
	SFSync::~SFSync()
	{
		Uninit();
	}

	bool SFSync::Init()
	{
		Uninit();
		SFMutexScoped lock(&__mutex);
		_impl = new(std::nothrow) SFSyncImpl();
		if (_impl == NULL)
			return false;
		_impl->_threads.clear();
		_impl->_msgs.clear();
		_impl->_state = kStateLoop;
		__errorCode = 0;
		__errorMsg = "";
		return true;
	}

	bool SFSync::Uninit()
	{
		__mutex.Enter();
		if (_impl != NULL)
		{
			_impl->_state = kStateTerm;
			for (int i = 0; i < (int)_impl->_threads.size(); i++)
			{
				SDL_Thread* thread = _impl->_threads[i]._thread;
				__mutex.Leave();
				SDL_WaitThread(thread, NULL);
				__mutex.Enter();
			}
			_impl->_threads.clear();
			_impl->_msgs.clear();
			delete _impl;
			_impl = NULL;
		}
		__mutex.Leave();
		return true;
	}

	bool SFSync::Test()
	{
		SFMutexScoped mutex(&__mutex);
		if (_impl == NULL)
			return false;
		return _impl->_state == kStateLoop;
	}

	bool SFSync::Loop(SFThread* thread)
	{
		while (true)
		{
			__mutex.Enter();
			if (_impl == NULL)
			{
				__mutex.Leave();
				return false;
			}
			else if (_impl->_state == kStatePoll)
			{
				thread->_state = kStatePoll;
				__mutex.Leave();
				SDL_Delay(1);
				continue;
			}
			else if (_impl->_state == kStateLoop)
			{
				thread->_state = kStateLoop;
				__mutex.Leave();
				return true;
			}
			else if (_impl->_state == kStateTerm)
			{
				thread->_state = kStateTerm;
				__mutex.Leave();
				return false;
			}
			else
			{
				__mutex.Leave();
				return false;
			}
		}
	}

	bool SFSync::Poll(SFThread* thread, SFMsg& msg)
	{
		while (true)
		{
			__mutex.Enter();
			if (_impl == NULL)
			{
				__mutex.Leave();
				return false;
			}
			else if (_impl->_state == kStatePoll)
			{
				thread->_state = kStatePoll;
				bool flag = true;
				for (int i = 0; i < (int)_impl->_threads.size(); i++)
				{
					if (_impl->_threads[i]._state != kStatePoll)
					{
						flag = false;
						break;
					}
				}
				if (flag == false)
				{
					__mutex.Leave();
					SDL_Delay(1);
					continue;
				}
				else if (_impl->_msgs.size() == 0)
				{
					_impl->_state = kStateLoop;
					__mutex.Leave();
					SDL_Delay(1);
					continue;
				}
				else
				{
					msg = _impl->_msgs.front();
					_impl->_msgs.pop_front();
					__mutex.Leave();
					return true;
				}
			}
			else if (_impl->_state == kStateLoop)
			{
				thread->_state = kStateLoop;
				if (_impl->_msgs.size() > 0)
					_impl->_state = kStatePoll;
				__mutex.Leave();
				SDL_Delay(1);
				continue;
			}
			else if (_impl->_state == kStateTerm)
			{
				thread->_state = kStateTerm;
				__mutex.Leave();
				return false;
			}
			else
			{
				__mutex.Leave();
				return false;
			}
		}
	}

	static int SDLThreadFunc(void* param)
	{
		SFThread* thread = (SFThread*)param;
		thread->_func(thread->_sync, thread, thread->_param);
		return 0;
	}

	bool SFSync::Spawn(const char* name, SFFunc func, void* param)
	{
		SFMutexScoped lock(&__mutex);
		if (_impl == NULL || name == NULL || func == NULL)
			return false;
		SFThread thread;
		thread._sync = this;
		thread._thread = SDL_CreateThread(SDLThreadFunc, name, &thread);
		if (thread._thread == NULL)
			return false;
		thread._name = name;
		thread._func = func;
		thread._param = param;
		_impl->_threads.push_back(thread);
		return true;
	}

	bool SFSync::Send(const SFMsg& msg)
	{
		SFMutexScoped lock(&__mutex);
		if (_impl == NULL)
			return false;
		_impl->_msgs.push_back(msg);
		return true;
	}

	bool SFSync::Error(int errorCode, const char* errorMsg)
	{
		SFMutexScoped lock(&__mutex);
		if (_impl == NULL)
			return true;
		_impl->_state = kStateTerm;
		__errorCode = errorCode;
		__errorMsg = errorMsg;
		return true;
	}

	bool SFSync::Term()
	{
		SFMutexScoped lock(&__mutex);
		if (_impl == NULL)
			return true;
		_impl->_state = kStateTerm;
		return true;
	}

	long long SFSync::ErrorCode()
	{
		return __errorCode;
	}

	const char* SFSync::ErrorMsg()
	{
		return __errorMsg.c_str();
	}
}

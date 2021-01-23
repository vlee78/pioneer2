#pragma warning (disable:4819)

#include "SFSync.h"


namespace pioneer
{
	static long long __errorCode = 0;
	static std::string __errorMsg = "";

	SFSync::SFSync()
	{
		SFMutexScoped lock(&_mutex);
		_threads.clear();
		_msgs.clear();
		_state = kStateLoop;
		__errorCode = 0;
		__errorMsg = "";
	}
	
	SFSync::~SFSync()
	{
		_mutex.Enter();
		_state = kStateTerm;
		for (int i = 0; i < (int)_threads.size(); i++)
		{
			SDL_Thread* thread = _threads[i]._thread;
			_mutex.Leave();
			SDL_WaitThread(thread, NULL);
			_mutex.Enter();
		}
		_threads.clear();
		_msgs.clear();
		_mutex.Leave();
	}

	bool SFSync::Test()
	{
		SFMutexScoped mutex(&_mutex);
		return _state == kStateLoop;
	}

	bool SFSync::Loop(SFThread* thread)
	{
		while (true)
		{
			_mutex.Enter();
			if (_state == kStatePoll)
			{
				thread->_state = kStatePoll;
				_mutex.Leave();
				SDL_Delay(10);
				continue;
			}
			else if (_state == kStateLoop)
			{
				thread->_state = kStateLoop;
				_mutex.Leave();
				return true;
			}
			else if (_state == kStateTerm)
			{
				thread->_state = kStateTerm;
				_mutex.Leave();
				return false;
			}
		}
	}

	bool SFSync::Poll(SFThread* thread, SFMsg& msg)
	{
		while (true)
		{
			_mutex.Enter();
			if (_state == kStatePoll)
			{
				thread->_state = kStatePoll;
				bool flag = true;
				for (int i = 0; i < (int)_threads.size(); i++)
				{
					if (_threads[i]._state != kStatePoll)
					{
						flag = false;
						break;
					}
				}
				if (flag == false)
				{
					_mutex.Leave();
					SDL_Delay(10);
					continue;
				}
				if (_msgs.size() == 0)
				{
					_state = kStateLoop;
					_mutex.Leave();
					SDL_Delay(10);
					continue;
				}
				msg = _msgs.front();
				_msgs.pop_front();
				_mutex.Leave();
				return true;
			}
			else if (_state == kStateLoop)
			{
				thread->_state = kStateLoop;
				if (_msgs.size() > 0)
					_state = kStatePoll;
				_mutex.Leave();
				SDL_Delay(10);
				continue;
			}
			else if (_state == kStateTerm)
			{
				thread->_state = kStateTerm;
				_mutex.Leave();
				return false;
			}
		}
		return true;
	}

	static int SDLThreadFunc(void* param)
	{
		SFThread* thread = (SFThread*)param;
		thread->_func(thread->_sync, thread->_param);
		return 0;
	}

	bool SFSync::Spawn(const char* name, SFFunc func, void* param)
	{
		SFMutexScoped lock(&_mutex);
		if (name == NULL || func == NULL)
			return false;
		SFThread thread;
		thread._sync = this;
		thread._thread = SDL_CreateThread(SDLThreadFunc, name, &thread);
		if (thread._thread == NULL)
			return false;
		thread._name = name;
		thread._func = func;
		thread._param = param;
		_threads.push_back(thread);
		return true;
	}

	void SFSync::Send(const SFMsg& msg)
	{
		SFMutexScoped lock(&_mutex);
		_msgs.push_back(msg);
	}

	void SFSync::Error(int errorCode, const char* errorMsg)
	{
		SFMutexScoped lock(&_mutex);
		_state = kStateTerm;
		__errorCode = errorCode;
		__errorMsg = errorMsg;
	}

	void SFSync::Term()
	{
		SFMutexScoped lock(&_mutex);
		_state = kStateTerm;
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

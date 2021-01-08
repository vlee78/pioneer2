#pragma warning (disable:4819)

#include "SFMutex.h"
#include <new>
#include <thread>
#include <atomic>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace pioneer
{
	class SFMutex::SFMutexImpl
	{
	public:
		std::atomic_flag _flag;
#if defined(_WIN32) || defined(_WIN64)
		DWORD _dwTlsIndex;
#else
		pthread_key_t _tlskey;
#endif
	};

	SFMutex::SFMutex()
	{
		_impl = new(std::nothrow) SFMutexImpl();
		if (_impl != NULL)
		{
			_impl->_flag.clear();
#if defined(_WIN32) || defined(_WIN64)
			_impl->_dwTlsIndex = TlsAlloc();
			TlsSetValue(_impl->_dwTlsIndex, (LPVOID)0);
#else
			_impl->_tlskey = 0;
			pthread_key_create(&_impl->_tlskey, NULL);
			pthread_setspecific(_impl->_tlskey, (const void *)0);
#endif
		}
	}

	SFMutex::~SFMutex()
	{
		if (_impl != NULL)
		{
#if defined(_WIN32) || defined(_WIN64)
			TlsFree(_impl->_dwTlsIndex);
#else
			pthread_key_delete(_impl->_tlskey);
#endif
			if (_impl != NULL)
			{
				delete _impl;
				_impl = NULL;
			}
		}
	}

	void SFMutex::Enter(bool yield)
	{
		if (_impl != NULL)
		{
			if (_impl->_flag.test_and_set(std::memory_order_acquire))
			{
#if defined(_WIN32) || defined(_WIN64)
				long long tls = (long long)TlsGetValue(_impl->_dwTlsIndex);
#else
				long tls = (long)pthread_getspecific(_impl->_tlskey);
#endif
				if (tls > 0)
				{
					tls++;
#if defined(_WIN32) || defined(_WIN64)
					TlsSetValue(_impl->_dwTlsIndex, (LPVOID)tls);
#else
					pthread_setspecific(_impl->_tlskey, (const void *)tls);
#endif
					return;
				}
				while (_impl->_flag.test_and_set(std::memory_order_acquire))
				{
					if (yield)
						std::this_thread::yield();
				}
			}
#if defined(_WIN32) || defined(_WIN64)
			TlsSetValue(_impl->_dwTlsIndex, (LPVOID)1);
#else
			pthread_setspecific(_impl->_tlskey, (const void *)1);
#endif
		}
	}

	void SFMutex::Leave()
	{
		if (_impl != NULL)
		{
#if defined(_WIN32) || defined(_WIN64)
			long long tls = (long long)TlsGetValue(_impl->_dwTlsIndex);
#else
			long tls = (long)pthread_getspecific(_impl->_tlskey);
#endif
			tls--;
			if (tls < 0) tls = 0;
			if (tls == 0)
			{
				_impl->_flag.clear(std::memory_order_release);
			}
#if defined(_WIN32) || defined(_WIN64)
			TlsSetValue(_impl->_dwTlsIndex, (LPVOID)tls);
#else
			pthread_setspecific(_impl->_tlskey, (const void *)tls);
#endif
		}
	}

	SFMutexScoped::SFMutexScoped(SFMutex* mutex, bool yield) : _mutex(mutex)
	{
		if (_mutex != NULL)
			_mutex->Enter(yield);
	}

	SFMutexScoped::~SFMutexScoped()
	{
		if (_mutex != NULL)
			_mutex->Leave();
	}

	void SFMutexScoped::LeaveEnter(bool yield)
	{
		if (_mutex != NULL)
		{
			_mutex->Leave();
			std::this_thread::yield();
			_mutex->Enter(yield);
		}
	}
}

#pragma warning (disable:4819)

#include "SFQueue.h"

namespace pioneer
{
	SFMessageQueue::SFMessageQueue()
	{
		SFMutexScoped lock(&_mutex);
	}

	SFMessageQueue::~SFMessageQueue()
	{
		SFMutexScoped lock(&_mutex);
	}

	bool SFMessageQueue::HasMessage()
	{
		SFMutexScoped lock(&_mutex);
		return _list.size() > 0;
	}

	bool SFMessageQueue::PopMessage(SFMessage& message)
	{
		SFMutexScoped lock(&_mutex);
		if (_list.size() == 0)
			return false;
		message = _list.front();
		_list.pop_front();
		return true;
	}

	void SFMessageQueue::PushMessage(const SFMessage& message)
	{
		SFMutexScoped lock(&_mutex);
		_list.push_back(message);
	}
}

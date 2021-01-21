#pragma once

extern "C"
{
	#include "libavformat/avformat.h"
	#include "libavcodec/avcodec.h"
	#include "libswscale/swscale.h"
	#include "libavutil/imgutils.h"
	#include "libswresample/swresample.h"
	#include "libavutil/opt.h"
}

#include "SFMutex.h"
#include <list>

namespace pioneer
{
	typedef enum 
	{
		kMsgQuit	= 0,
		kMsgPause	= 1,
		kMsgPlay	= 2,
		kMsgSeek	= 3,
	} SFMsgId;

	typedef struct
	{
		SFMsgId _id;
		int _intParam;
		long long _longParam;
	} SFMessage;

	class SFMessageQueue
	{
	public:
		SFMessageQueue();
		~SFMessageQueue();

		bool HasMessage();
		bool PopMessage(SFMessage& message);
		void PushMessage(const SFMessage& message);
	private:
		SFMutex _mutex;
		std::list<SFMessage> _list;
	};
}



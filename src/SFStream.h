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

namespace pioneer
{
	class SFStream
	{
	public:
		static SFStream* Create(AVStream* stream);
		static bool Release(SFStream*& sfstream);

		bool Reset();
		int GetSampleRate();
		int GetStreamIndex();
		AVRational* GetTimebase();
		long long GetHead(AVRational* timebase);
		long long GetTail(AVRational* timebase);
		long long GetSize(AVRational* timebase);
		bool Consume(AVPacket*& packet);
		AVFrame* Peak();
		bool Pop();

	private:
		AVStream* _stream;
		AVRational* _timebase;
		AVCodecContext* _context;
		SFMutex _mutex;
	};
}



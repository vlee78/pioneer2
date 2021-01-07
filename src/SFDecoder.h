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
	class SFDecoder
	{
	public:
        enum Flag
        {
            Default = 0,
            NoAudio = 1,
            NoVideo = 2,
        };

		enum State
		{
			Buffering	= 0,
			Ready		= 1,
			Closeed		= 2,
		};

	public:
		SFDecoder();
		~SFDecoder();
		long long Init(const char* filename, Flag flag = Default);
		bool Uninit();

		bool Forward(long long timestamp);
		AVFrame* DequeueAudio();
		AVFrame* DequeueVideo();

	private:
		class SFDecoderImpl;
		SFDecoderImpl* _impl;
	};
}



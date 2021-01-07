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
			Closed		= 0,
			Buffering	= 1,
			Ready		= 2,
			Eof			= 3,
		};

	public:
		SFDecoder();
		~SFDecoder();
		long long Init(const char* filename, Flag flag = Default);
		bool Uninit();

		AVStream* GetAudioStream();
		AVStream* GetVideoStream();

		long long Forward(long long timestamp);
		long long GetTimestamp();
		AVRational* GetTimebase();
		State GetState();

		AVFrame* DequeueAudio();
		AVFrame* DequeueVideo();

	private:
		class SFDecoderImpl;
		SFDecoderImpl* _impl;
	};
}



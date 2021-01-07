#pragma once

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

	public:
		SFDecoder();
		~SFDecoder();
		long long Init(const char* filename, Flag flag = Default);
		bool Uninit();

	private:
		class SFDecoderImpl;
		SFDecoderImpl* _impl;
	};
}



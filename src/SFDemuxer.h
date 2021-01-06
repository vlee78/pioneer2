#pragma once

namespace pioneer
{
	class SFDemuxer
	{
	public:
        enum Flag
        {
            Default = 0,
            NoAudio = 1,
            NoVideo = 2,
        };

	public:
		SFDemuxer();
		~SFDemuxer();
		long long Init(const char* filename, Flag flag = Default);
		bool Uninit();

	private:
		class SFDemuxerImpl;
		SFDemuxerImpl* _impl;
	};
}



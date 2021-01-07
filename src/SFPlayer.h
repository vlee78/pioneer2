#pragma once

namespace pioneer
{
	class SFPlayer
	{
	public:
		enum State
		{
			Closed		= 0,
			Paused		= 1,
			Buffering	= 2,
			Playing		= 3,
		};
        
		enum Flag
		{
			Default = 0,
			NoAudio = 1,
			NoVideo = 2,
		};

	public:
		SFPlayer();
		~SFPlayer();
		long long Init(const char* filename, Flag flag = Default);
		void Uninit();

	private:
		class SFPlayerImpl;
		SFPlayerImpl* _impl;
	};
}



#pragma once

namespace pioneer
{
	class SFReplayer
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
			Buffering = 0,
			Ready = 1,
			Closeed = 2,
		};

		SFReplayer();
		~SFReplayer();
		long long Init(const char* filename, Flag flag = Default);
		bool Uninit();

	private:
		class SFReplayerImpl;
		SFReplayerImpl* _impl;
	};
}



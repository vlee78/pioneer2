#pragma once

namespace pioneer
{
	class SFReplayer
	{
	public:
		enum Flag
		{
			Audio	= 1,
			Video	= 2,
			Both	= 3,
		};

		enum State
		{
			Buffering = 0,
			Ready = 1,
			Closeed = 2,
		};

		SFReplayer();
		~SFReplayer();
		long long Init(const char* filename, Flag flag = Both);
		bool Uninit();

	private:
		class SFReplayerImpl;
		SFReplayerImpl* _impl;
	};
}



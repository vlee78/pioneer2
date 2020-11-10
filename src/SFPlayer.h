#pragma once

namespace pioneer
{
	class SFPlayer
	{
	public:
		enum State
		{
			Ready		= 0,
			Paused		= 1,
			Buffering	= 2,
			Playing		= 3,
		};

	public:
		SFPlayer();
		~SFPlayer();
		long long Init(const char* filename, bool videoEnabled = true, bool audioEnabled = true);
		void Uninit();

		State GetState();
		long long PausePlay(bool play);
		float GetCurrent();
		float GetTotal();
		long long SeekTo(float seek);

	private:
		class SFPlayerImpl;
		SFPlayerImpl* _impl;
	};
}



#pragma once

namespace pioneer
{
	class SFPlayer
	{
	public:
		SFPlayer();
		~SFPlayer();
		long long Init(const char* filename, bool videoEnabled = true, bool audioEnabled = true);
		void Uninit();

	private:
		class SFPlayerImpl;
		SFPlayerImpl* _impl;
	};
}



#pragma once

namespace pioneer
{
	class SFPlayer
	{
	public:
		SFPlayer();
		~SFPlayer();
		long long Init(const char* filename);
		void Uninit();

	private:
		class SFPlayerImpl;
		SFPlayerImpl* _impl;
	};
}



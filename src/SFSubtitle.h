#pragma once

extern "C" 
{
	#include "libavutil/rational.h"
}

namespace pioneer
{
	class SFSubtitle
	{
	public:
		static long long ProcessSubtitle(const char* filepath);

	};
}



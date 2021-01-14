#pragma once

extern "C" 
{
	#include "libavutil/rational.h"
}

namespace pioneer
{
	class SFUtils
	{
	public:

		static int GreatestCommonDivisor(int a, int b);

		static int LeastCommonMultiple(int a, int b);

		static long long SecondsToSamples(double seconds, int sampleRate);

		static AVRational CommonTimebase(AVRational* audioTimebase, AVRational* videoTimebase);

		static long long TimestampToTimestamp(long long timestamp, AVRational* frTimebase, AVRational* toTimebase);

		static long long SecondsToTimestamp(double seconds, AVRational* timebase);

		static double TimestampToSeconds(long long timestamp, AVRational* timebase);

		static long long TimestampToMs(long long timestamp, AVRational* timebase);

		static long long SamplesToTimestamp(long long samples, int sampleRate, AVRational* timebase);

		static long long TimestampToSamples(long long timestamp, int sampleRate, AVRational* timebase);

		static long long GetTickNanos();

		static long long GetTickMs();
		
		static double GetTickSeconds();
	};
}



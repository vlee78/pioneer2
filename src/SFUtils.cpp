#pragma warning (disable:4819)

#include "SFUtils.h"
#include <stdio.h>
#include <chrono>

namespace pioneer
{
	int SFUtils::GreatestCommonDivisor(int a, int b)
	{
		int na = a;
		int nb = b;
		if (nb) while ((na %= nb) && (nb %= na));
		return na + nb;
	}

	int SFUtils::LeastCommonMultiple(int a, int b)
	{
		return (int)(((long long)a * b) / GreatestCommonDivisor(a, b));
	}

	long long SFUtils::SecondsToSamples(double seconds, int sampleRate)
	{
		return (long long)(seconds * sampleRate);
	}

	AVRational SFUtils::CommonTimebase(AVRational* audioTimebase, AVRational* videoTimebase)
	{
		AVRational res;
		if (audioTimebase != NULL && videoTimebase != NULL)
			res = { LeastCommonMultiple(audioTimebase->num, videoTimebase->num), LeastCommonMultiple(audioTimebase->den, videoTimebase->den) };
		else if (audioTimebase != NULL)
			res = { audioTimebase->num, audioTimebase->den };
		else if (videoTimebase != NULL)
			res = { videoTimebase->num, videoTimebase->den };
		return res;
	}

	long long SFUtils::TimestampToTimestamp(long long timestamp, AVRational* frTimebase, AVRational* toTimebase)
	{
		return timestamp * frTimebase->num * toTimebase->den / (frTimebase->den * toTimebase->num);
	}

	long long SFUtils::SecondsToTimestamp(double seconds, AVRational* timebase)
	{
		return (long long)(seconds * timebase->den / timebase->num);
	}

	double SFUtils::TimestampToSeconds(long long timestamp, AVRational* timebase)
	{
		return timestamp * timebase->num / (double)timebase->den;
	}

	long long SFUtils::TimestampToMs(long long timestamp, AVRational* timebase)
	{
		return timestamp * 1000 * timebase->num / timebase->den;
	}

	long long SFUtils::SamplesToTimestamp(long long samples, int sampleRate, AVRational* timebase)
	{
		return (samples * timebase->den) / ((long long)sampleRate * timebase->num);
	}

	long long SFUtils::TimestampToSamples(long long timestamp, int sampleRate, AVRational* timebase)
	{
		return timestamp * sampleRate * timebase->num / timebase->den;
	}

	long long SFUtils::GetTickNanos()
	{
		return std::chrono::high_resolution_clock::now().time_since_epoch().count();
	}

	long long SFUtils::GetTickMs()
	{
		return std::chrono::high_resolution_clock::now().time_since_epoch().count() / 1000000;
	}

	double SFUtils::GetTickSeconds()
	{
		return std::chrono::high_resolution_clock::now().time_since_epoch().count() / 1000000000.0;
	}
}

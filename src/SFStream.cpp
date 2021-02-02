#pragma warning (disable:4819)

#include "SFStream.h"
#include "SFMutex.h"
#include <new>
#include <string>
#include <vector>
#include <list>

namespace pioneer
{
	SFStream* SFStream::Create(AVStream* stream)
	{
		if (stream == NULL)
			return NULL;
		SFStream* sfstream = new(std::nothrow) SFStream();
		if (sfstream == NULL)
			return NULL;
		AVCodec* codec = NULL;
		AVCodecContext* context = NULL;
		if ((codec = avcodec_find_decoder(stream->codecpar->codec_id)) == NULL ||
			(context = avcodec_alloc_context3(codec)) == NULL ||
			avcodec_parameters_to_context(context, stream->codecpar) != 0 ||
			avcodec_open2(context, codec, NULL) != 0)
		{
			if (context != NULL)
				avcodec_close(context);
			return NULL;
		}
		sfstream->_stream = stream;
		sfstream->_timebase = &stream->time_base;
		sfstream->_context = context;
		return sfstream;
	}

	bool SFStream::Release(SFStream*& sfstream)
	{
		if (sfstream == NULL)
			return true;
		if (sfstream->_context != NULL)
			avcodec_close(sfstream->_context);
		delete sfstream;
		sfstream = NULL;
		return true;
	}

	bool SFStream::Reset()
	{
		avcodec_flush_buffers(_context);
		return true;
	}

	int SFStream::GetSampleRate()
	{
		return _stream->codecpar->sample_rate;
	}

	int SFStream::GetStreamIndex()
	{
		return _stream->index;
	}

	AVRational* SFStream::GetTimebase()
	{
		return _timebase;
	}

	long long SFStream::GetHead(AVRational* timebase)
	{

	}

	long long SFStream::GetTail(AVRational* timebase)
	{

	}

	long long SFStream::GetSize(AVRational* timebase)
	{

	}

	bool SFStream::Consume(AVPacket*& packet, )
	{
		if (avcodec_send_packet(_context, packet) != 0)
			return false;
		while (true)
		{
			if (audioFrame == NULL && (audioFrame = av_frame_alloc()) == NULL && sync->Error(-14, ""))
				break;
			ret = avcodec_receive_frame(impl->_audioCodecCtx, audioFrame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			else if (ret < 0 && sync->Error(-15, ""))
				break;
			long long timestamp = SFUtils::TimestampToTimestamp(audioFrame->pts, impl->_audioTimebase, &impl->_commonTimebase);
			if (timestamp >= impl->_timestamp)
			{
				impl->_mutex.Enter();
				impl->_audioFrames.push_back(audioFrame);
				impl->_mutex.Leave();
				audioFrame = NULL;
			}
			else
			{
				av_frame_unref(audioFrame);
				av_frame_free(&audioFrame);
				audioFrame = NULL;
			}
		}
	}

	AVFrame* SFStream::Peak()
	{

	}

	bool SFStream::Pop()
	{

	}
}

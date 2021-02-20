#pragma warning (disable:4819)

#include "SFStream.h"
#include "SFMutex.h"
#include "SFUtils.h"
#include <new>
#include <string>
#include <vector>
#include <list>

namespace pioneer
{
	SFStream* SFStream::Create(AVStream* stream, AVRational* timebase)
	{
		if (stream == NULL || timebase == NULL)
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

	bool SFStream::Push(AVPacket*& packet)
	{
		if (packet == NULL || avcodec_send_packet(_context, packet) != 0)
			return false;
		av_packet_unref(packet);
		av_packet_free(&packet);
		packet = NULL;
		while (true)
		{
			if (_frame == NULL && (_frame = av_frame_alloc()) == NULL)
				return false;
			int ret = avcodec_receive_frame(_context, _frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				return true;
			else if (ret < 0)
				return false;
			long long timestamp = SFUtils::TimestampToTimestamp(_frame->pts, &_stream->time_base, _timebase);
			if (timestamp >= _timestamp)
			{
				_mutex.Enter();
				_frames.push_back(_frame);
				_mutex.Leave();
				_frame = NULL;
			}
			else
			{
				av_frame_unref(_frame);
				av_frame_free(&_frame);
				_frame = NULL;
			}
		}
	}

	AVFrame* SFStream::Pop()
	{
		AVFrame* frame = NULL;
		_mutex.Enter();
		if (_frames.size() > 0)
		{
			frame = _frames.front();
			_frames.pop_front();
		}
		_mutex.Leave();
		return frame;
	}
}

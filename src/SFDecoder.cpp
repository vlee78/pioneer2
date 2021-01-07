#pragma warning (disable:4819)

#include "SFDecoder.h"
#include "SFUtils.h"
#include "SDL.h"
#include <stdio.h>
#include <new>
#include <string>
#include <vector>
#include <list>
#include <chrono>

extern "C"
{
	#include "libavformat/avformat.h"
	#include "libavcodec/avcodec.h"
	#include "libswscale/swscale.h"
	#include "libavutil/imgutils.h"
	#include "libswresample/swresample.h"
	#include "libavutil/opt.h"
}

namespace pioneer
{
	class SFDecoder::SFDecoderImpl
	{
	public:
		State _state;
		AVFormatContext* _demuxFormatCtx;
		AVStream* _audioStream;
		AVStream* _videoStream;
		AVCodecContext* _audioCodecCtx;
		AVCodecContext* _videoCodecCtx;
		AVRational _commonTimebase;
		AVRational _audioTimebase;
		AVRational _videoTimebase;
		bool _looping;
		SDL_Thread* _thread;
		long long _timestamp;
		long long _errorcode;
		std::list<AVPacket*> _audioPackets;
		std::list<AVPacket*> _videoPackets;
		std::list<AVFrame*> _audioFrames;
		std::list<AVFrame*> _videoFrames;

		static int DecodeThread(void* param)
		{
			SFDecoderImpl* impl = (SFDecoderImpl*)param;
			AVFrame* audioFrame = NULL;
			AVFrame* videoFrame = NULL;
			while (impl->_looping)
			{
				if (impl->_state == Eof)
				{
					SDL_Delay(100);
					continue;
				}
				long long thresholdTimestamp = impl->_timestamp + SFUtils::SecondsToTimestamp(5.0, &impl->_commonTimebase);
				long long audioTailTimestamp = (impl->_audioFrames.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_audioFrames.back()->pts, &impl->_audioTimebase, &impl->_commonTimebase));
				long long videoTailTimestamp = (impl->_videoFrames.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_videoFrames.back()->pts, &impl->_audioTimebase, &impl->_commonTimebase));
				if (audioTailTimestamp >= thresholdTimestamp && videoTailTimestamp >= thresholdTimestamp)
				{
					if (impl->_state == Buffering)
						impl->_state = Ready;
					SDL_Delay(0);
					continue;
				}
				long long audioHeadTimestamp = (impl->_audioPackets.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_audioPackets.front()->pts, &impl->_audioTimebase, &impl->_commonTimebase));
				long long videoHeadTimestamp = (impl->_videoPackets.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_videoPackets.front()->pts, &impl->_videoTimebase, &impl->_commonTimebase));
				if (audioHeadTimestamp >= 0 && (videoHeadTimestamp < 0 || audioHeadTimestamp <= videoHeadTimestamp))
				{
					AVPacket* audioPacket = impl->_audioPackets.front();
					impl->_audioPackets.pop_front();
					int ret = avcodec_send_packet(impl->_audioCodecCtx, audioPacket);
					av_packet_unref(audioPacket);
					av_packet_free(&audioPacket);
					audioPacket = NULL;
					if (ret != 0)
					{
						impl->_looping = false;
						impl->_errorcode = -1;
						break;
					}
					while (impl->_looping)
					{
						if (audioFrame == NULL && (audioFrame = av_frame_alloc()) == NULL)
						{
							impl->_looping = false;
							impl->_errorcode = -1;
							break;
						}
						ret = avcodec_receive_frame(impl->_audioCodecCtx, audioFrame);
						if (ret < 0)
						{
							if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
							{
								impl->_looping = false;
								impl->_errorcode = -2;
							}
							break;
						}
						long long timestamp = SFUtils::TimestampToTimestamp(audioFrame->pts, &impl->_audioTimebase, &impl->_commonTimebase);
						if (timestamp >= impl->_timestamp)
						{
							impl->_audioFrames.push_back(audioFrame);
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
				else if (videoHeadTimestamp >= 0 && (audioHeadTimestamp < 0 || videoHeadTimestamp <= audioHeadTimestamp))
				{
					AVPacket* videoPacket = impl->_videoPackets.front();
					impl->_videoPackets.pop_front();
					int ret = avcodec_send_packet(impl->_videoCodecCtx, videoPacket);
					av_packet_unref(videoPacket);
					av_packet_free(&videoPacket);
					videoPacket = NULL;
					if (ret != 0)
					{
						impl->_looping = false;
						impl->_errorcode = -3;
						break;
					}
					while (impl->_looping)
					{
						if (videoFrame == NULL && (videoFrame = av_frame_alloc()) == NULL)
						{
							impl->_looping = false;
							impl->_errorcode = -3;
							break;
						}
						int ret = avcodec_receive_frame(impl->_videoCodecCtx, videoFrame);
						if (ret < 0)
						{
							if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
							{
								impl->_looping = false;
								impl->_errorcode = -3;
							}
							break;
						}
						long long timestamp = SFUtils::TimestampToTimestamp(videoFrame->pts, &impl->_videoTimebase, &impl->_commonTimebase);
						if (timestamp >= impl->_timestamp)
						{
							impl->_videoFrames.push_back(videoFrame);
							videoFrame = NULL;
						}
						else
						{
							av_frame_unref(videoFrame);
							av_frame_free(&videoFrame);
							videoFrame = NULL;
						}
					}
				}
				else
				{
					AVPacket* packet = av_packet_alloc();
					if (packet == NULL)
					{
						impl->_looping = false;
						impl->_errorcode = -1;
						break;
					}
					int ret = av_read_frame(impl->_demuxFormatCtx, packet);
					if (ret == 0)
					{
						if (impl->_audioStream && packet->stream_index == impl->_audioStream->index)
						{
							impl->_audioPackets.push_back(packet);
							packet = NULL;
						}
						else if (impl->_videoStream && packet->stream_index == impl->_videoStream->index)
						{
							impl->_videoPackets.push_back(packet);
							packet = NULL;
						}
					}
					else if (ret == AVERROR_EOF)
					{
						impl->_state = Eof;
					}
					else
					{
						impl->_looping = false;
						impl->_errorcode = -2;
					}
					if (packet != NULL)
					{
						av_packet_unref(packet);
						av_packet_free(&packet);
						packet = NULL;
					}
				}
			}
			if (audioFrame != NULL)
			{
				av_frame_unref(audioFrame);
				av_frame_free(&audioFrame);
				audioFrame = NULL;
			}
			if (videoFrame != NULL)
			{
				av_frame_unref(videoFrame);
				av_frame_free(&videoFrame);
				videoFrame = NULL;
			}
			return 0;
		}
	};

	SFDecoder::SFDecoder()
	{
		_impl = NULL;
	}

	SFDecoder::~SFDecoder()
	{
		Uninit();
	}

	long long SFDecoder::Init(const char* filename, Flag flag)
	{
		Uninit();
		_impl = new(std::nothrow) SFDecoderImpl();
		if (_impl == NULL)
			return -1;
		_impl->_state = Closed;
		_impl->_demuxFormatCtx = NULL;
		_impl->_audioStream = NULL;
		_impl->_videoStream = NULL;
		_impl->_audioCodecCtx = NULL;
		_impl->_videoCodecCtx = NULL;
		_impl->_commonTimebase = { 0, 0 };
		_impl->_audioTimebase = { 0, 0 };
		_impl->_videoTimebase = { 0, 0 };
		_impl->_looping = true;
		_impl->_thread = NULL;
		_impl->_timestamp = 0;
		_impl->_errorcode = 0;
		
		if (avformat_open_input(&_impl->_demuxFormatCtx, filename, NULL, NULL) != 0 && Uninit())
			return -2;
		if (avformat_find_stream_info(_impl->_demuxFormatCtx, NULL) < 0 && Uninit())
			return -3;
		for (int i = 0; i < (int)_impl->_demuxFormatCtx->nb_streams; i++)
		{
			if (flag != NoAudio && _impl->_audioStream == NULL && _impl->_demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
				_impl->_audioStream = _impl->_demuxFormatCtx->streams[i];
			if (flag != NoVideo && _impl->_videoStream == NULL && _impl->_demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
				_impl->_videoStream = _impl->_demuxFormatCtx->streams[i];
		}
		if (((_impl->_audioStream == NULL && _impl->_videoStream == NULL) || (_impl->_audioStream == NULL && flag == NoVideo) || (_impl->_videoStream == NULL && flag == NoAudio)) && Uninit())
			return -4;
		_impl->_commonTimebase = SFUtils::CommonTimebase((_impl->_audioStream == NULL ? NULL : &_impl->_audioStream->time_base), (_impl->_videoStream == NULL ? NULL : &_impl->_videoStream->time_base));
		if (_impl->_audioStream != NULL)
		{
			_impl->_audioTimebase = _impl->_audioStream->time_base;
			AVCodec* audioCodec = avcodec_find_decoder(_impl->_audioStream->codecpar->codec_id);
			if (audioCodec == NULL && Uninit())
				return -5;
			if ((_impl->_audioCodecCtx = avcodec_alloc_context3(audioCodec)) == NULL && Uninit())
				return -6;
			if (avcodec_parameters_to_context(_impl->_audioCodecCtx, _impl->_audioStream->codecpar) != 0 && Uninit())
				return -7;
			if (avcodec_open2(_impl->_audioCodecCtx, audioCodec, NULL) != 0 && Uninit())
				return -8;
		}
		if (_impl->_videoStream != NULL)
		{
			_impl->_videoTimebase = _impl->_videoStream->time_base;
			AVCodec* videoCodec = avcodec_find_decoder(_impl->_videoStream->codecpar->codec_id);
			if (videoCodec == NULL && Uninit())
				return -9;
			if ((_impl->_videoCodecCtx = avcodec_alloc_context3(videoCodec)) == NULL && Uninit())
				return -10;
			if (avcodec_parameters_to_context(_impl->_videoCodecCtx, _impl->_videoStream->codecpar) != 0 && Uninit())
				return -11;
			if (avcodec_open2(_impl->_videoCodecCtx, videoCodec, NULL) != 0 && Uninit())
				return -12;
		}
		_impl->_state = Buffering;
		_impl->_thread = SDL_CreateThread(SFDecoderImpl::DecodeThread, "Decoder", _impl);
		if (_impl->_thread == NULL && Uninit())
			return -13;
        return 0;
	}

	bool SFDecoder::Uninit()
	{
		if (_impl != NULL)
		{
			_impl->_looping = false;
			if (_impl->_thread != NULL)
			{
				SDL_WaitThread(_impl->_thread, NULL);
				_impl->_thread = NULL;
			}
			for (auto it = _impl->_audioPackets.begin(); it != _impl->_audioPackets.end(); av_packet_unref(*it), av_packet_free(&*it), it++);
			for (auto it = _impl->_videoPackets.begin(); it != _impl->_videoPackets.end(); av_packet_unref(*it), av_packet_free(&*it), it++);
			for (auto it = _impl->_audioFrames.begin(); it != _impl->_audioFrames.end(); av_frame_unref(*it), av_frame_free(&*it), it++);
			for (auto it = _impl->_videoFrames.begin(); it != _impl->_videoFrames.end(); av_frame_unref(*it), av_frame_free(&*it), it++);
			delete _impl;
			_impl = NULL;
		}
		return true;
	}

	AVStream* SFDecoder::GetAudioStream()
	{
		if (_impl == NULL)
			return NULL;
		return _impl->_audioStream;
	}
	
	AVStream* SFDecoder::GetVideoStream()
	{
		if (_impl == NULL)
			return NULL;
		return _impl->_videoStream;
	}

	long long SFDecoder::Forward(long long timestamp)
	{
		if (_impl == NULL || timestamp < 0)
			return -1;
		_impl->_timestamp += timestamp;
		return _impl->_timestamp;
	}

	long long SFDecoder::GetTimestamp()
	{
		if (_impl == NULL)
			return -1;
		return _impl->_timestamp;
	}

	SFDecoder::State SFDecoder::GetState()
	{
		if (_impl == NULL)
			return Closed;
		return _impl->_state;
	}

	AVRational* SFDecoder::GetTimebase()
	{
		if (_impl == NULL)
			return NULL;
		return &_impl->_commonTimebase;
	}

	AVFrame* SFDecoder::DequeueAudio()
	{
		if (_impl == NULL || _impl->_audioStream == NULL || _impl->_state == Buffering)
			return NULL;
		if (_impl->_audioFrames.size() == 0)
		{
			if (_impl->_state == Ready)
				_impl->_state = Buffering;
			return NULL;
		}
		AVFrame* frame = _impl->_audioFrames.front();
		_impl->_audioFrames.pop_front();
		return frame;
	}

	AVFrame* SFDecoder::DequeueVideo()
	{
		if (_impl == NULL || _impl->_videoStream == NULL || _impl->_state == Buffering)
			return NULL;
		if (_impl->_videoFrames.size() == 0)
		{
			if (_impl->_state == Ready)
				_impl->_state = Buffering;
			return NULL;
		}
		AVFrame* frame = _impl->_videoFrames.front();
		_impl->_videoFrames.pop_front();
		return frame;
	}
}

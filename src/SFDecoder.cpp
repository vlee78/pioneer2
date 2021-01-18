#pragma warning (disable:4819)

#include "SFDecoder.h"
#include "SFMutex.h"
#include "SFUtils.h"
#include "SDL.h"
#include <stdio.h>
#include <new>
#include <string>
#include <vector>
#include <list>
#include <chrono>
#include <thread>

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
	static SFMutex __mutex;
	static SFMutex __lock;

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
		int _audioFrameSize;
		bool _looping;
		SDL_Thread* _thread;
		long long _timestamp;
		long long _errorcode;
		std::list<AVFrame*> _audioFrames;
		std::list<AVFrame*> _videoFrames;

		static bool error(SFDecoderImpl* impl, long long errorCode)
		{
			impl->_looping = false;
			impl->_errorcode = errorCode;
			return true;
		}


		static long long Seek(SFDecoderImpl* impl, double seekto)
		{
			long long errorCode = 0;
			if (impl->_videoStream != NULL)
			{
				long long timestamp = SFUtils::SecondsToTimestamp(seekto, &impl->_videoTimebase);
				long long video_key_pts = -1;
				long long video_min_pts = -1;
				long long lastseek = -1;
				long long seekts = timestamp;
				AVPacket* packet = NULL;
				while (true)
				{
					if (seekts >= 0)
					{
						video_key_pts = -1;
						video_min_pts = -1;
						if (lastseek >= 0 && seekts >= lastseek)
						{
							errorCode = -1;
							break;
						}
						if (av_seek_frame(impl->_demuxFormatCtx, impl->_videoStream->index, seekts, AVSEEK_FLAG_BACKWARD) != 0)
						{
							errorCode = -2;
							break;
						}
						lastseek = seekts;
						seekts = -1;
					}
					packet = av_packet_alloc();
					if (packet == NULL)
					{
						errorCode = -3;
						break;
					}
					bool hit = false;
					int ret = av_read_frame(impl->_demuxFormatCtx, packet);
					if (ret == 0)
					{
						if (impl->_audioStream && packet->stream_index == impl->_audioStream->index)
						{
						}
						else if (impl->_videoStream && packet->stream_index == impl->_videoStream->index)
						{
							if (video_key_pts >= 0 && packet->flags & AV_PKT_FLAG_KEY)
								hit = true;
							if (video_key_pts == -1 && packet->flags & AV_PKT_FLAG_KEY)
							{
								video_key_pts = packet->pts;
								if (video_key_pts <= timestamp)
									hit = true;
							}
							if (video_min_pts == -1 || packet->pts < video_min_pts)
								video_min_pts = packet->pts;
						}
					}
					else if (ret == AVERROR_EOF)
					{
						if (video_key_pts == -1)
						{
							errorCode = -4;
							break;
						}
						hit = true;
					}
					else
					{
						errorCode = -4;
						break;
					}
					av_packet_unref(packet);
					av_packet_free(&packet);
					packet = NULL;
					if (hit)
					{
						if (video_key_pts < 0)
						{
							errorCode = -5;
							break;
						}
						if (video_key_pts <= timestamp)
							break;
						seekts = video_min_pts - 1;
					}
				}
				if (packet != NULL)
				{
					av_packet_unref(packet);
					av_packet_free(&packet);
					packet = NULL;
				}
				if (av_seek_frame(impl->_demuxFormatCtx, impl->_videoStream->index, lastseek, AVSEEK_FLAG_BACKWARD) != 0)
					errorCode = -6;
			}
			else if (impl->_audioStream)
			{
			}
			if (errorCode == 0)
				impl->_timestamp = SFUtils::SecondsToTimestamp(seekto, &impl->_commonTimebase);
			return errorCode;
		}

		static int DecodeThread(void* param)
		{
			SFDecoderImpl* impl = (SFDecoderImpl*)param;
			AVPacket* packet = NULL;
			AVFrame* audioFrame = NULL;
			AVFrame* videoFrame = NULL;
			while (impl->_looping)
			{
				__mutex.Enter();
				if (impl->_state == Eof)
				{
					__mutex.Leave();
					SDL_Delay(1000);
					continue;
				}
				long long thresholdTimestamp = impl->_timestamp + SFUtils::SecondsToTimestamp(5.0, &impl->_commonTimebase);
				long long audioTailTimestamp = (impl->_audioStream == NULL ? LLONG_MAX : (impl->_audioFrames.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_audioFrames.back()->pts, &impl->_audioTimebase, &impl->_commonTimebase)));
				long long videoTailTimestamp = (impl->_videoStream == NULL ? LLONG_MAX : (impl->_videoFrames.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_videoFrames.back()->pts, &impl->_videoTimebase, &impl->_commonTimebase)));
				if (audioTailTimestamp >= thresholdTimestamp && videoTailTimestamp >= thresholdTimestamp)
				{
					if (impl->_state == Buffering)
						impl->_state = Ready;
					__mutex.Leave();
					SDL_Delay(5);
					continue;
				}
				__mutex.Leave();
				if (packet == NULL && (packet = av_packet_alloc()) == NULL && error(impl, -1))
					break;
				int ret = av_read_frame(impl->_demuxFormatCtx, packet);
				if (ret == AVERROR_EOF)
				{
					__mutex.Enter();
					impl->_state = Eof;
					__mutex.Leave();
					continue;
				}
				else if (ret != 0 && error(impl, -2))
					break;
				if (impl->_audioStream && packet->stream_index == impl->_audioStream->index)
				{
					if (avcodec_send_packet(impl->_audioCodecCtx, packet) != 0 && error(impl, -1))
						break;
					while (impl->_looping)
					{
						if (audioFrame == NULL && (audioFrame = av_frame_alloc()) == NULL && error(impl, -1))
							break;
						ret = avcodec_receive_frame(impl->_audioCodecCtx, audioFrame);
						if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
							break;
						else if (ret < 0 && error(impl, -1))
							break;
						long long timestamp = SFUtils::TimestampToTimestamp(audioFrame->pts, &impl->_audioTimebase, &impl->_commonTimebase);
						if (timestamp >= impl->_timestamp)
						{
							__mutex.Enter();
							impl->_audioFrames.push_back(audioFrame);
							__mutex.Leave();
							audioFrame = NULL;
						}
						else
						{
							av_frame_unref(audioFrame);
							av_frame_free(&audioFrame);
							audioFrame = NULL;
						}
					}
					SDL_Delay(1);
				}
				else if (impl->_videoStream && packet->stream_index == impl->_videoStream->index)
				{
					if (avcodec_send_packet(impl->_videoCodecCtx, packet) != 0 && error(impl, -1))
						break;
					while (impl->_looping)
					{
						if (videoFrame == NULL && (videoFrame = av_frame_alloc()) == NULL && error(impl, -1))
							break;
						int ret = avcodec_receive_frame(impl->_videoCodecCtx, videoFrame);
						if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
							break;
						if (ret < 0 && error(impl, -1))
							break;
						long long timestamp = SFUtils::TimestampToTimestamp(videoFrame->pts, &impl->_videoTimebase, &impl->_commonTimebase);
						if (timestamp >= impl->_timestamp)
						{
							__mutex.Enter();
							impl->_videoFrames.push_back(videoFrame);
							__mutex.Leave();
							videoFrame = NULL;
						}
						else
						{
							av_frame_unref(videoFrame);
							av_frame_free(&videoFrame);
							videoFrame = NULL;
						}
					}
					SDL_Delay(1);
				}
				av_packet_unref(packet);
				av_packet_free(&packet);
				packet = NULL;
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
		SFMutexScoped lock(&__lock);
		_impl = NULL;
	}

	SFDecoder::~SFDecoder()
	{
		SFMutexScoped lock(&__lock);
		Uninit();
	}

	static int CalcAudioFrameSize(const char* filename)
	{
		AVFormatContext* demuxFormatCtx = NULL;
		AVStream* audioStream = NULL;
		int frameSize = 0;
		if (avformat_open_input(&demuxFormatCtx, filename, NULL, NULL) != 0)
			goto end;
		if (avformat_find_stream_info(demuxFormatCtx, NULL) < 0)
			goto end;
		for (int i = 0; i < (int)demuxFormatCtx->nb_streams; i++)
		{
			if (demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				audioStream = demuxFormatCtx->streams[i];
				break;
			}
		}
		if (audioStream == NULL)
			goto end;
		frameSize = audioStream->codecpar->frame_size;
		if (frameSize == 0)
		{
			AVPacket packet;
			while (true)
			{
				if (av_read_frame(demuxFormatCtx, &packet) != 0)
				{
					av_packet_unref(&packet);
					break;
				}
				if (packet.stream_index != audioStream->index)
				{
					av_packet_unref(&packet);
					continue;
				}
				frameSize = (int)SFUtils::TimestampToSamples(packet.duration, audioStream->codecpar->sample_rate, &audioStream->time_base);
				av_packet_unref(&packet);
				break;
			}
		}
	end:
		audioStream = NULL;
		if (demuxFormatCtx != NULL)
		{
			avformat_close_input(&demuxFormatCtx);
			demuxFormatCtx = NULL;
		}
		if (frameSize == 0)
			frameSize = 1536;
		return frameSize;
	}

	long long SFDecoder::Init(const char* filename, Flag flag)
	{
		SFMutexScoped lock(&__lock);
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
		_impl->_audioFrameSize = CalcAudioFrameSize(filename);
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
		_impl->_commonTimebase = SFUtils::CommonTimebase((_impl->_audioStream == NULL ? NULL : &_impl->_audioStream->time_base), (_impl->_videoStream == NULL ? NULL : &_impl->_videoStream->time_base));

		_impl->_state = Buffering;

		//SFDecoderImpl::Seek(_impl, 30.0);

		_impl->_thread = SDL_CreateThread(SFDecoderImpl::DecodeThread, "Decoder", _impl);
		if (_impl->_thread == NULL && Uninit())
			return -13;
        return 0;
	}

	bool SFDecoder::Uninit()
	{
		SFMutexScoped lock(&__lock);
		if (_impl != NULL)
		{
			_impl->_looping = false;
			if (_impl->_thread != NULL)
			{
				SDL_WaitThread(_impl->_thread, NULL);
				_impl->_thread = NULL;
			}
			for (auto it = _impl->_audioFrames.begin(); it != _impl->_audioFrames.end(); av_frame_unref(*it), av_frame_free(&*it), it++);
			for (auto it = _impl->_videoFrames.begin(); it != _impl->_videoFrames.end(); av_frame_unref(*it), av_frame_free(&*it), it++);
			delete _impl;
			_impl = NULL;
		}
		return true;
	}

	AVStream* SFDecoder::GetAudioStream()
	{
		SFMutexScoped lock(&__lock);
		if (_impl == NULL)
			return NULL;
		return _impl->_audioStream;
	}
	
	AVStream* SFDecoder::GetVideoStream()
	{
		SFMutexScoped lock(&__lock);
		if (_impl == NULL)
			return NULL;
		return _impl->_videoStream;
	}

	long long SFDecoder::Forward(long long timestamp)
	{
		//SFMutexScoped lock(&__lock);
		//SFMutexScoped mutex(&__mutex);
		if (_impl == NULL || timestamp < 0)
			return -1;
		_impl->_timestamp += timestamp;
		return _impl->_timestamp;
	}

	long long SFDecoder::GetTimestamp()
	{
		//SFMutexScoped lock(&__lock);
		//SFMutexScoped mutex(&__mutex);
		if (_impl == NULL)
			return -1;
		return _impl->_timestamp;
	}

	SFDecoder::State SFDecoder::GetState()
	{
		//SFMutexScoped lock(&__lock);
		//SFMutexScoped mutex(&__mutex);
		if (_impl == NULL)
			return Closed;
		return _impl->_state;
	}

	AVRational SFDecoder::GetCommonTimebase()
	{
		if (_impl == NULL)
			return{ 0, 0 };
		return _impl->_commonTimebase;
	}

	AVRational SFDecoder::GetAudioTimebase()
	{
		if (_impl == NULL)
			return{ 0, 0 };
		return _impl->_audioTimebase;
	}

	AVRational SFDecoder::GetVideoTimebase()
	{
		if (_impl == NULL)
			return{ 0, 0 };
		return _impl->_videoTimebase;
	}

	int SFDecoder::GetAudioFrameSize()
	{
		if (_impl == NULL)
			return 0;
		return _impl->_audioFrameSize;
	}

	int SFDecoder::GetAudioQueueSize()
	{
		SFMutexScoped lock(&__lock);
		SFMutexScoped mutex(&__mutex);
		if (_impl == NULL || _impl->_audioStream == NULL)
			return 0;
		return (int)_impl->_audioFrames.size();
	}

	int SFDecoder::GetVideoQueueSize()
	{
		SFMutexScoped lock(&__lock);
		SFMutexScoped mutex(&__mutex);
		if (_impl == NULL || _impl->_videoStream == NULL)
			return 0;
		return (int)_impl->_videoFrames.size();
	}

	AVFrame* SFDecoder::DequeueAudio()
	{
		//SFMutexScoped lock(&__lock);
		//SFMutexScoped mutex(&__mutex);
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
		//SFMutexScoped lock(&__lock);
		//SFMutexScoped mutex(&__mutex);
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

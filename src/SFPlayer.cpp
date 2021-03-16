#pragma warning (disable:4819)

#include "SFPlayer.h"
#include "SFSync.h"
#include "SFUtils.h"
#include "SFMutex.h"
#include <stdio.h>
#include <new>
#include <string>
#include <vector>
#include <list>
#include <chrono>
#include <algorithm>
#include "SDL.h"

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
	enum MsgId
	{
		kMsgPauseResume			= 0,
		kMsgSeek				= 1,
		kMsgBackward			= 2,
		kMsgForward				= 3,
		kMsgRewind				= 4,
		kMsgWindowSizeChanged	= 5,
	};

	
	class StreamContext
	{
	public:
		AVStream* _stream;
		AVRational* _timebase;
		AVCodecContext* _codecCtx;
		AVFrame* _decodeFrame;
		std::list<AVFrame*> _queue;
		AVFrame* _renderFrame;

		static bool Create(StreamContext* streamContext, AVStream* stream)
		{

		}

		static bool Release(StreamContext*& streamContex)
		{

		}

		StreamContext()
		{
			_stream = NULL;
			_timebase = NULL;
			_codecCtx = NULL;
			_decodeFrame = NULL;
			_queue.clear();
			_renderFrame = NULL;
		}

		~StreamContext()
		{
			
		}

		void Reset()
		{

		}

		double Seconds()
		{

		}

		void Consume(AVPacket*& packet)
		{

		}

		AVFrame* Peek()
		{
			return NULL;
		}

		void Pop()
		{

		}
	};

	class SFPlayer::SFPlayerImpl
	{
	public:
		AVFormatContext* _demuxFormatCtx;
		int _audioDeviceSamples;
		AVStream* _audioStream;
		AVStream* _videoStream;
		AVCodecContext* _audioCodecCtx;
		AVCodecContext* _videoCodecCtx;
		AVRational* _audioTimebase;
		AVRational* _videoTimebase;
		AVRational _commonTimebase;
		SFSync _sync;
		SDL_Window* _renderWindow;
		SDL_Renderer* _renderRenderer;
		SDL_Texture* _renderTexture;
		long long _timestamp;
		std::list<AVFrame*> _audioFrames;
		std::list<AVFrame*> _videoFrames;
		State _state;
		SFMutex _mutex;
		AVFrame* _audioFrame;
		AVFrame* _videoFrame;
		AVFrame* _decodeAudioFrame;
		AVFrame* _decodeVideoFrame;
		long long _startTs;
		void* _hwnd;

		static long long Seek(SFPlayer::SFPlayerImpl* impl, double seekto)
		{
			long long errorCode = 0;
			if (impl->_videoStream != NULL)
			{
				long long timestamp = SFUtils::SecondsToTimestamp(seekto, impl->_videoTimebase);
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

		void ClearImplement(bool reopen)
		{
			for (auto it = this->_audioFrames.begin(); it != this->_audioFrames.end(); it = this->_audioFrames.erase(it))
			{
				av_frame_unref(*it);
				av_frame_free(&*it);
			}
			for (auto it = this->_videoFrames.begin(); it != this->_videoFrames.end(); it = this->_videoFrames.erase(it))
			{
				av_frame_unref(*it);
				av_frame_free(&*it);
			}
			if (this->_audioFrame != NULL)
			{
				av_frame_unref(this->_audioFrame);
				av_frame_free(&this->_audioFrame);
				this->_audioFrame = NULL;
			}
			if (this->_videoFrame != NULL)
			{
				av_frame_unref(this->_videoFrame);
				av_frame_free(&this->_videoFrame);
				this->_videoFrame = NULL;
			}
			if (this->_decodeAudioFrame != NULL)
			{
				av_frame_unref(this->_decodeAudioFrame);
				av_frame_free(&this->_decodeAudioFrame);
				this->_decodeAudioFrame = NULL;
			}
			if (this->_decodeVideoFrame != NULL)
			{
				av_frame_unref(this->_decodeVideoFrame);
				av_frame_free(&this->_decodeVideoFrame);
				this->_decodeVideoFrame = NULL;
			}
			if (reopen)
			{
				if (this->_audioStream != NULL)
				{
					avcodec_flush_buffers(this->_audioCodecCtx);
					/*
					if (this->_audioCodecCtx != NULL) avcodec_close(this->_audioCodecCtx);
					AVCodec* audioCodec = avcodec_find_decoder(this->_audioStream->codecpar->codec_id);
					this->_audioCodecCtx = avcodec_alloc_context3(audioCodec);
					avcodec_parameters_to_context(this->_audioCodecCtx, this->_audioStream->codecpar);
					avcodec_open2(this->_audioCodecCtx, audioCodec, NULL);
					*/
				}
				if (this->_videoStream != NULL)
				{
					avcodec_flush_buffers(this->_videoCodecCtx);
					/*
					if (this->_videoCodecCtx != NULL) avcodec_close(this->_videoCodecCtx);
					AVCodec* videoCodec = avcodec_find_decoder(this->_videoStream->codecpar->codec_id);
					this->_videoCodecCtx = avcodec_alloc_context3(videoCodec);
					avcodec_parameters_to_context(this->_videoCodecCtx, this->_videoStream->codecpar);
					avcodec_open2(this->_videoCodecCtx, videoCodec, NULL);
					*/
				}
			}
		}

		static void AudioDevice(void* userdata, Uint8* stream, int len)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)userdata;
			memset(stream, 0, len);
			int sampleRate = impl->_audioStream->codecpar->sample_rate;
			short* buffer0 = ((short*)stream) + 0;
			short* buffer1 = ((short*)stream) + 1;
			int bufchs = 2;
			int bufoff = 0;
			int bufmax = len / sizeof(short);

			AVFrame*& frame = impl->_audioFrame;
			int nb_samples = 0;
			while (impl->_sync.Test())
			{
				if (frame == NULL)
				{
					impl->_mutex.Enter();
					if (impl->_state == Playing && impl->_audioFrames.size() > 0)
					{
						frame = impl->_audioFrames.front();
						impl->_audioFrames.pop_front();
					}
					impl->_mutex.Leave();
				}
				if (frame == NULL)
					break;
				AVSampleFormat format = (AVSampleFormat)frame->format;
				int channels = frame->channels;
				nb_samples = frame->nb_samples;
				if (sampleRate != frame->sample_rate && impl->_sync.Error(-21, ""))
					break;
				int framemax = frame->nb_samples;
				int frameoff = frame->width;
				if (format == AV_SAMPLE_FMT_FLTP)
				{
					if (channels == 1)
					{
						float* buf = (float*)frame->data[0];
						for (; frameoff < framemax && bufoff < bufmax; frameoff++, bufoff += bufchs)
						{
							float val = buf[frameoff];
							int check = (int)(val * 32768.0f);
							check = (check < -32768 ? -32768 : (check > 32767 ? 32767 : check));
							buffer0[bufoff] = (short)check;
							buffer1[bufoff] = (short)check;
						}
					}
					else if (channels == 2)
					{
						float* buf0 = (float*)frame->data[0];
						float* buf1 = (float*)frame->data[1];
						for (; frameoff < framemax && bufoff < bufmax; frameoff++, bufoff += bufchs)
						{
							float val0 = buf0[frameoff];
							float val1 = buf1[frameoff];
							int check0 = (int)(val0 * 32768.0f);
							int check1 = (int)(val1 * 32768.0f);
							check0 = (check0 < -32768 ? -32768 : (check0 > 32767 ? 32767 : check0));
							check1 = (check1 < -32768 ? -32768 : (check1 > 32767 ? 32767 : check1));
							buffer0[bufoff] = (short)check0;
							buffer1[bufoff] = (short)check1;
						}
					}
					else if (channels == 6)
					{
						float* buf0 = (float*)frame->data[0];
						float* buf1 = (float*)frame->data[1];
						float* buf2 = (float*)frame->data[2];
						float* buf3 = (float*)frame->data[3];
						float* buf4 = (float*)frame->data[4];
						float* buf5 = (float*)frame->data[5];
						for (; frameoff < framemax && bufoff < bufmax; frameoff++, bufoff += bufchs)
						{
							float val0 = buf0[frameoff] + buf2[frameoff] + buf3[frameoff] + buf4[frameoff];
							float val1 = buf1[frameoff] + buf2[frameoff] + buf3[frameoff] + buf5[frameoff];
							int check0 = (int)(val0 * 32768.0f);
							int check1 = (int)(val1 * 32768.0f);
							check0 = (check0 < -32768 ? -32768 : (check0 > 32767 ? 32767 : check0));
							check1 = (check1 < -32768 ? -32768 : (check1 > 32767 ? 32767 : check1));
							buffer0[bufoff] = (short)check0;
							buffer1[bufoff] = (short)check1;
						}
					}
					else
					{
						impl->_sync.Error(-23, "");
						break;
					}
				}
				else if (format == AV_SAMPLE_FMT_S16P)
				{
					if (channels == 1)
					{
						short* buf0 = (short*)frame->data[0];
						for (; frameoff < framemax && bufoff < bufmax; frameoff++, bufoff += bufchs)
						{
							buffer0[bufoff] = buf0[frameoff];
							buffer1[bufoff] = buf0[frameoff];
						}
					}
					if (channels == 2)
					{
						short* buf0 = (short*)frame->data[0];
						short* buf1 = (short*)frame->data[1];
						for (; frameoff < framemax && bufoff < bufmax; frameoff++, bufoff += bufchs)
						{
							buffer0[bufoff] = buf0[frameoff];
							buffer1[bufoff] = buf1[frameoff];
						}
					}
					else
					{
						impl->_sync.Error(-24, "");
						break;
					}
				}
				else
				{
					impl->_sync.Error(-25, "");
					break;
				}
				frame->width = frameoff;
				if (frameoff >= framemax)
				{
					av_frame_unref(frame);
					av_frame_free(&frame);
					frame = NULL;
				}
				if (bufoff >= bufmax)
					break;
			}
			if (bufoff > 0)
			{
				long long samples = bufoff / bufchs;//总共消耗了frame queue多少sample
				long long shift = SFUtils::SamplesToTimestamp(samples, sampleRate, &impl->_commonTimebase);
				impl->_mutex.Enter();
				impl->_timestamp += shift;
				impl->_mutex.Leave();
			}
		}

		static void RenderThread(SFSync* sync, SFThread* thread, void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			AVFrame*& frame = impl->_videoFrame;
			while (sync->Loop(thread))
			{
				if (frame == NULL)
				{
					impl->_mutex.Enter();
					if (impl->_state == Playing && impl->_videoFrames.size() > 0)
					{
						frame = impl->_videoFrames.front();
						impl->_videoFrames.pop_front();
					}
					impl->_mutex.Leave();
				}
				if (frame == NULL)
				{
					continue;
				}
				long long timestamp = SFUtils::TimestampToTimestamp(frame->best_effort_timestamp, impl->_videoTimebase, &impl->_commonTimebase);
				long long duration = SFUtils::TimestampToTimestamp(frame->pkt_duration, impl->_videoTimebase, &impl->_commonTimebase);
				if (timestamp <= impl->_timestamp && impl->_renderRenderer != NULL && impl->_renderTexture != NULL)
				{
					if (SDL_UpdateYUVTexture(impl->_renderTexture, NULL, frame->data[0], frame->linesize[0], frame->data[1],
						frame->linesize[1], frame->data[2], frame->linesize[2]) != 0 && sync->Error(-41, ""))
						continue;
					int ret = SDL_RenderCopy(impl->_renderRenderer, impl->_renderTexture, NULL, NULL);
					if (ret != 0 && sync->Error(-42, ""))
						continue;
					SDL_RenderPresent(impl->_renderRenderer);
					av_frame_unref(frame);
					av_frame_free(&frame);
					frame = NULL;
				}
				SDL_Delay(5);
				if (impl->_audioStream == NULL)
				{
					//desc->_impl->_decoder.Forward(duration);
					//long long nanoTs = std::chrono::high_resolution_clock::now().time_since_epoch().count();
					//long long nanoDiff = nanoTs - desc->_impl->_ts;
					//double diffSecs = nanoDiff / (double)1000000000l;
					//if (nanoDiff > 0)
					//{
					//	desc->_impl->_time += diffSecs;
					//	desc->_impl->_ts = nanoTs;
					//}
				}
			}
		}

		static void DecodeThread(SFSync* sync, SFThread* thread, void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			AVPacket* packet = NULL;
			AVFrame*& audioFrame = impl->_decodeAudioFrame;
			AVFrame*& videoFrame = impl->_decodeVideoFrame;
			while (sync->Loop(thread))
			{
				impl->_mutex.Enter();
				long long thresholdTimestamp = impl->_timestamp + SFUtils::SecondsToTimestamp(3.0, &impl->_commonTimebase);
				long long audioTailTimestamp = (impl->_audioStream == NULL ? LLONG_MAX : (impl->_audioFrames.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_audioFrames.back()->pts, impl->_audioTimebase, &impl->_commonTimebase)));
				long long videoTailTimestamp = (impl->_videoStream == NULL ? LLONG_MAX : (impl->_videoFrames.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_videoFrames.back()->pts, impl->_videoTimebase, &impl->_commonTimebase)));
				if (audioTailTimestamp >= thresholdTimestamp && videoTailTimestamp >= thresholdTimestamp)
				{
					if (impl->_state == Buffering)
						impl->_state = Playing;
					impl->_mutex.Leave();
					SDL_Delay(50);
					continue;
				}
				impl->_mutex.Leave();
				if (packet == NULL && (packet = av_packet_alloc()) == NULL && sync->Error(-11, ""))
					continue;
				int ret = av_read_frame(impl->_demuxFormatCtx, packet);
				if (ret == AVERROR_EOF)
				{
					impl->_mutex.Enter();
					impl->_state = Eof;
					impl->_mutex.Leave();
					SDL_Delay(5);
					continue;
				}
				else if (ret != 0 && sync->Error(-12, ""))
					continue;
				if (impl->_audioStream && packet->stream_index == impl->_audioStream->index)
				{
					if (avcodec_send_packet(impl->_audioCodecCtx, packet) != 0 && sync->Error(-13, ""))
						continue;
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
				else if (impl->_videoStream && packet->stream_index == impl->_videoStream->index)
				{
					if (avcodec_send_packet(impl->_videoCodecCtx, packet) != 0 && sync->Error(-16, ""))
						continue;
					while (true)
					{
						if (videoFrame == NULL && (videoFrame = av_frame_alloc()) == NULL && sync->Error(-17, ""))
							break;
						int ret = avcodec_receive_frame(impl->_videoCodecCtx, videoFrame);
						if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
							break;
						if (ret < 0 && sync->Error(-18, ""))
							break;
						long long timestamp = SFUtils::TimestampToTimestamp(videoFrame->pts, impl->_videoTimebase, &impl->_commonTimebase);
						if (timestamp >= impl->_timestamp)
						{
							impl->_mutex.Enter();
							impl->_videoFrames.push_back(videoFrame);
							impl->_mutex.Leave();
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
		}

		static void PollThread(SFSync* sync, SFThread* thread, void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			SFMsg msg;
			while (sync->Poll(thread, msg))
			{
				if (msg._id == kMsgPauseResume)
				{

				}
				else if (msg._id == kMsgSeek)
				{
					impl->ClearImplement(true);
					if (SFPlayerImpl::Seek(impl, msg._dparam) != 0 && sync->Error(-51, ""))
						continue;
				}
				else if (msg._id == kMsgBackward)
				{
					impl->ClearImplement(true);
					double toSeconds = SFUtils::TimestampToSeconds(impl->_timestamp, &impl->_commonTimebase) - msg._dparam;
					toSeconds = std::max(toSeconds, 0.0);
					if (SFPlayerImpl::Seek(impl, toSeconds) != 0 && sync->Error(-52, ""))
						continue;
					impl->_state = Buffering;
				}
				else if (msg._id == kMsgForward)
				{
					impl->ClearImplement(true);
					double toSeconds = SFUtils::TimestampToSeconds(impl->_timestamp, &impl->_commonTimebase) + msg._dparam;
					toSeconds = std::max(toSeconds, 0.0);
					if (SFPlayerImpl::Seek(impl, toSeconds) != 0 && sync->Error(-52, ""))
						continue;
					impl->_state = Buffering;
				}
				else if (msg._id == kMsgRewind)
				{
					impl->ClearImplement(true);
					if (SFPlayerImpl::Seek(impl, 0.0) != 0 && sync->Error(-52, ""))
						continue;
					impl->_state = Buffering;
				}
				else if (msg._id == kMsgWindowSizeChanged)
				{
					if (impl->_renderTexture != NULL)
					{
						SDL_DestroyTexture(impl->_renderTexture);
						impl->_renderTexture = NULL;
					}
					if (impl->_renderRenderer != NULL)
					{
						SDL_RenderClear(impl->_renderRenderer);
						impl->_renderRenderer = NULL;
					}
					if (impl->_renderWindow != NULL)
					{
						SDL_DestroyWindow(impl->_renderWindow);
						impl->_renderWindow = NULL;
					}
					int width = impl->_videoStream->codecpar[impl->_videoStream->index].width;
					int height = impl->_videoStream->codecpar[impl->_videoStream->index].height;
					if (impl->_hwnd == NULL)
						impl->_renderWindow = SDL_CreateWindow("RenderWindow", 100, 100, width / 2, height / 2, SDL_WINDOW_SHOWN);
					else
						impl->_renderWindow = SDL_CreateWindowFrom(impl->_hwnd);
					if (impl->_renderWindow == NULL && sync->Error(-3, ""))
						continue;
					impl->_renderRenderer = SDL_CreateRenderer(impl->_renderWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
					if (impl->_renderRenderer == NULL && sync->Error(-4, ""))
						continue;
					impl->_renderTexture = SDL_CreateTexture(impl->_renderRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
					if (impl->_renderTexture == NULL && sync->Error(-5, ""))
						continue;
				}
				else
				{
					SDL_Delay(100);
				}
			}
		}

		static void SdlThread(SFSync* sync, SFThread* thread, void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			while (sync->Loop(thread))
			{
				SDL_Event event;
				if (SDL_PollEvent(&event))
				{
					switch (event.type)
					{
					case SDL_KEYDOWN:
						if (event.key.keysym.sym == SDLK_LEFT)
							sync->Send({ kMsgBackward, 0, 0, 0.0f, 5.0 });
						else if (event.key.keysym.sym == SDLK_RIGHT)
							sync->Send({ kMsgForward, 0, 0, 0.0f, 5.0 });
						else if (event.key.keysym.sym == SDLK_DOWN)
							sync->Send({ kMsgRewind, 0, 0, 0.0f, 0.0 });
						else if (event.key.keysym.sym == SDLK_UP)
							sync->Send({ kMsgPauseResume, 0, 0, 0.0f, 0.0 });
						break;
					case SDL_QUIT:
						sync->Term();
						break;
					case SDL_WINDOWEVENT:
						switch (event.window.event)
						{
						case SDL_WINDOWEVENT_SIZE_CHANGED:
							sync->Send({ kMsgWindowSizeChanged, event.window.data1, event.window.data2, 0.0f, 0.0 }, true);
							break;
						};
						break;
					};
				}
			}
		}
	};

	SFPlayer::SFPlayer()
	{
		_impl = NULL;
	}
	
	SFPlayer::~SFPlayer()
	{
		Uninit();
	}

	static int CalAudioDeviceSamples(AVFormatContext* demuxFormatCtx, AVStream* audioStream)
	{
		int frameSize = 1024;
		if (audioStream != NULL)
		{
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
				av_seek_frame(demuxFormatCtx, 0, 0, 0);
			}
		}
		return frameSize;
	}

	long long SFPlayer::Init(const char* filename, Flag flag, void* hwnd)
	{
		Uninit();
		_impl = new(std::nothrow) SFPlayerImpl();
		if (_impl == NULL)
			return -1;
		_impl->_demuxFormatCtx = NULL;
		_impl->_audioDeviceSamples = 0;
		_impl->_audioStream = NULL;
		_impl->_videoStream = NULL;
		_impl->_audioCodecCtx = NULL;
		_impl->_videoCodecCtx = NULL;
		_impl->_audioTimebase = NULL;
		_impl->_videoTimebase = NULL;
		_impl->_commonTimebase = { 0, 0 };
		_impl->_renderWindow = NULL;
		_impl->_renderRenderer = NULL;
		_impl->_renderTexture = NULL;
		_impl->_timestamp = 0;
		_impl->_audioFrames.clear();
		_impl->_videoFrames.clear();
		_impl->_state = Closed;
		_impl->_audioFrame = NULL;
		_impl->_videoFrame = NULL;
		_impl->_decodeAudioFrame = NULL;
		_impl->_decodeVideoFrame = NULL;
		_impl->_hwnd = hwnd;
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
			_impl->_audioTimebase = &_impl->_audioStream->time_base;
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
			_impl->_videoTimebase = &_impl->_videoStream->time_base;
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
		_impl->_audioDeviceSamples = CalAudioDeviceSamples(_impl->_demuxFormatCtx, _impl->_audioStream);
		printf("audioDeviceSamples = %d\n", _impl->_audioDeviceSamples);
		_impl->_state = Buffering;
		_impl->_commonTimebase = SFUtils::CommonTimebase(_impl->_audioTimebase, _impl->_videoTimebase);
		
		if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 && Uninit())
			return -13;
		if (_impl->_hwnd && SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl") != SDL_TRUE && Uninit())
			return -14;

		int width = _impl->_videoStream->codecpar[_impl->_videoStream->index].width;
		int height = _impl->_videoStream->codecpar[_impl->_videoStream->index].height;
		if (_impl->_hwnd)
			_impl->_renderWindow = SDL_CreateWindowFrom(_impl->_hwnd);
		else
			_impl->_renderWindow = SDL_CreateWindow("RenderWindow", 100, 100, width / 2, height / 2, SDL_WINDOW_SHOWN);
		if (_impl->_renderWindow == NULL && Uninit())
			return -15;
		_impl->_renderRenderer = SDL_CreateRenderer(_impl->_renderWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (_impl->_renderRenderer == NULL && Uninit())
			return -16;
		_impl->_renderTexture = SDL_CreateTexture(_impl->_renderRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);		
		if (_impl->_renderTexture == NULL && Uninit())
			return -17;
		
		SDL_AudioSpec want;
		SDL_zero(want);
		want.freq = _impl->_audioStream->codecpar->sample_rate;
		want.format = AUDIO_S16SYS;
		want.channels = 2;
		want.samples = _impl->_audioDeviceSamples;
		want.callback = SFPlayerImpl::AudioDevice;
		want.userdata = _impl;
		want.padding = 52428;
		SDL_AudioSpec real;
		SDL_zero(real);
		SDL_AudioDeviceID audioDeviceId = SDL_OpenAudioDevice(NULL, 0, &want, &real, 0);
		if (audioDeviceId == 0 && Uninit())
			return -18;
		SDL_PauseAudioDevice(audioDeviceId, 0);
		
		if (_impl->_sync.Init() == false && Uninit())
			return -13;
		if (_impl->_sync.Spawn("SdlThread", SFPlayerImpl::SdlThread, _impl) == false && Uninit())
			return -14;
		if (_impl->_videoStream && _impl->_sync.Spawn("RenderThread", SFPlayerImpl::RenderThread, _impl) == false && _impl->_sync.Error(-6, "") && Uninit())
			return -15;
		if (_impl->_sync.Spawn("DecodeThread", SFPlayerImpl::DecodeThread, _impl) == false && _impl->_sync.Error(-7, "") && Uninit())
			return -16;
		if (_impl->_sync.Spawn("PollThread", SFPlayerImpl::PollThread, _impl) == false && _impl->_sync.Error(-8, "") && Uninit())
			return -17;
		return 0;
	}

	bool SFPlayer::Uninit()
	{
		if (_impl != NULL)
		{
			_impl->_sync.Uninit();

			
			if (_impl->_renderTexture != NULL)
				SDL_DestroyTexture(_impl->_renderTexture);
			if (_impl->_renderRenderer != NULL)
				SDL_RenderClear(_impl->_renderRenderer);
			if (_impl->_renderWindow != NULL)
				SDL_DestroyWindow(_impl->_renderWindow);
			_impl->_renderTexture = NULL;
			_impl->_renderWindow = NULL;
			_impl->_renderRenderer = NULL;
			SDL_Quit();


			_impl->ClearImplement(false);
			if (_impl->_audioCodecCtx != NULL) avcodec_close(_impl->_audioCodecCtx);
			if (_impl->_videoCodecCtx != NULL) avcodec_close(_impl->_videoCodecCtx);
			if (_impl->_demuxFormatCtx != NULL) avformat_close_input(&_impl->_demuxFormatCtx);
			_impl->_renderTexture = NULL;
			_impl->_renderRenderer = NULL;
			_impl->_renderWindow = NULL;
			_impl->_commonTimebase = { 0,0 };
			_impl->_audioTimebase = NULL;
			_impl->_videoTimebase = NULL;
			_impl->_audioStream = NULL;
			_impl->_videoStream = NULL;
			_impl->_audioCodecCtx = NULL;
			_impl->_videoCodecCtx = NULL;
			_impl->_demuxFormatCtx = NULL;
			_impl->_hwnd = NULL;
			delete _impl;
			_impl = NULL;
		}
		return true;
	}

	bool SFPlayer::Seek(double seconds)
	{
		if (_impl == NULL)
			return false;
		return _impl->_sync.Send({kMsgSeek, 0, 0, 0.0f, seconds});
	}

	bool SFPlayer::Reszie()
	{
		if (_impl == NULL)
			return false;
		return _impl->_sync.Send({ kMsgWindowSizeChanged, 0, 0, 0.0f, 0.0 }, true);
	}

	bool SFPlayer::GetState(State& state, double& seconds)
	{
		if (_impl == NULL)
			return false;
		state = _impl->_state;
		seconds = SFUtils::TimestampToSeconds(_impl->_timestamp, &_impl->_commonTimebase);
		return true;
	}

	bool SFPlayer::GetDuration(double& seconds)
	{
		if (_impl == NULL)
			return false;
		seconds = _impl->_demuxFormatCtx->duration / (double)AV_TIME_BASE;
		return true;
	}
}

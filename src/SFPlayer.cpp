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
	class SFPlayer::SFPlayerImpl
	{
	public:
		AVFormatContext* _demuxFormatCtx;
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
				if (timestamp <= impl->_timestamp)
				{
					if (SDL_UpdateYUVTexture(impl->_renderTexture, NULL, frame->data[0], frame->linesize[0], frame->data[1],
						frame->linesize[1], frame->data[2], frame->linesize[2]) != 0 && sync->Error(-41, ""))
						continue;
					if (SDL_RenderCopy(impl->_renderRenderer, impl->_renderTexture, NULL, NULL) != 0 && sync->Error(-42, ""))
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
			AVFrame* audioFrame = NULL;
			AVFrame* videoFrame = NULL;
			while (sync->Loop(thread))
			{
				impl->_mutex.Enter();
				long long thresholdTimestamp = impl->_timestamp + SFUtils::SecondsToTimestamp(5.0, &impl->_commonTimebase);
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
				SDL_Delay(100);
			}
		}

		static void MainThread(SFSync* sync, SFThread* thread, void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 && sync->Error(-1, ""))
				goto end;
			if (impl->_audioStream)
			{
				SDL_AudioSpec want;
				SDL_zero(want);
				want.freq = impl->_audioStream->codecpar->sample_rate;
				want.format = AUDIO_S16SYS;
				want.channels = 2;
				want.samples = 1024;// impl->_decoder.GetAudioFrameSize();
				want.callback = SFPlayerImpl::AudioDevice;
				want.userdata = impl;
				want.padding = 52428;
				SDL_AudioSpec real;
				SDL_zero(real);
				SDL_AudioDeviceID audioDeviceId = SDL_OpenAudioDevice(NULL, 0, &want, &real, 0);
				if (audioDeviceId == 0 && sync->Error(-2, ""))
					goto end;
				SDL_PauseAudioDevice(audioDeviceId, 0);
			}
			if (impl->_videoStream)
			{
				int width = impl->_videoStream->codecpar[impl->_videoStream->index].width;
				int height = impl->_videoStream->codecpar[impl->_videoStream->index].height;
				impl->_renderWindow = SDL_CreateWindow("RenderWindow", 100, 100, width/2, height/2, SDL_WINDOW_SHOWN);
				if (impl->_renderWindow == NULL && sync->Error(-3, ""))
					goto end;
				impl->_renderRenderer = SDL_CreateRenderer(impl->_renderWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
				if (impl->_renderRenderer == NULL && sync->Error(-4, ""))
					goto end;
				impl->_renderTexture = SDL_CreateTexture(impl->_renderRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
				if (impl->_renderTexture == NULL && sync->Error(-5, ""))
					goto end;
			}
			if (impl->_videoStream && sync->Spawn("RenderThread", SFPlayerImpl::RenderThread, impl) == false && sync->Error(-6, ""))
				goto end;
			if (sync->Spawn("DecodeThread", SFPlayerImpl::DecodeThread, impl) == false && sync->Error(-7, ""))
				goto end;
			if (sync->Spawn("PollThread", SFPlayerImpl::PollThread, impl) == false && sync->Error(-8, ""))
				goto end;
			while (sync->Loop(thread))
			{
				SDL_Event event;
				if (SDL_PollEvent(&event))
				{
					switch (event.type)
					{
					case SDL_QUIT:
						sync->Term();
						break;
					};
				}
			}
		end:
			if (impl->_renderTexture != NULL) SDL_DestroyTexture(impl->_renderTexture);
			if (impl->_renderRenderer != NULL) SDL_RenderClear(impl->_renderRenderer);
			if (impl->_renderWindow != NULL) SDL_DestroyWindow(impl->_renderWindow);
			impl->_renderWindow = NULL;
			impl->_renderRenderer = NULL;
			SDL_Quit();
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

	long long SFPlayer::Init(const char* filename, Flag flag)
	{
		Uninit();
		_impl = new(std::nothrow) SFPlayerImpl();
		if (_impl == NULL)
			return -1;
		_impl->_demuxFormatCtx = NULL;
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
		_impl->_state = Buffering;
		_impl->_commonTimebase = SFUtils::CommonTimebase(_impl->_audioTimebase, _impl->_videoTimebase);
		if (_impl->_sync.Init() == false && Uninit())
			return -13;
		if (_impl->_sync.Spawn("MainThread", SFPlayerImpl::MainThread, _impl) == false && Uninit())
			return -14;
		return 0;
	}

	bool SFPlayer::Uninit()
	{
		if (_impl != NULL)
		{
			_impl->_sync.Uninit();
			for (auto it = _impl->_audioFrames.begin(); it != _impl->_audioFrames.end(); it = _impl->_audioFrames.erase(it))
			{
				av_frame_unref(*it);
				av_frame_free(&*it);
			}
			for (auto it = _impl->_videoFrames.begin(); it != _impl->_videoFrames.end(); it = _impl->_videoFrames.erase(it))
			{
				av_frame_unref(*it);
				av_frame_free(&*it);
			}
			if (_impl->_audioFrame != NULL)
			{
				av_frame_unref(_impl->_audioFrame);
				av_frame_free(&_impl->_audioFrame);
				_impl->_audioFrame = NULL;
			}
			if (_impl->_videoFrame != NULL)
			{
				av_frame_unref(_impl->_videoFrame);
				av_frame_free(&_impl->_videoFrame);
				_impl->_videoFrame = NULL;
			}
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
			delete _impl;
			_impl = NULL;
		}
		return true;
	}
}

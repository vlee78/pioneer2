#pragma warning (disable:4819)

#include "SFReplayer.h"
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
	enum ThreadSync
	{
		kSyncWillSpin	= 0,
		kSyncDidSpin	= 1,
		kSyncWillRun	= 2,
		kSyncDidRun		= 3,
		kSyncWillEnd	= 4,
		kSyncDidEnd		= 5,
	};

	class SFReplayer::SFReplayerImpl
	{
	public:
		bool _looping;
		long long _errorCode;
		SFDecoder _decoder;
		SDL_Thread* _mainThread;

		ThreadSync _syncDecoder;

		//0:要求空转
		//1:实际空转
		//2:要求运转
		//3:实际运转
		//4:要求退出
		//5:最终退出

		struct Desc
		{
			SFReplayerImpl* _impl;
			AVStream* _audioStream;
			AVStream* _videoStream;
			AVFrame* _audioFrame;
			AVFrame* _videoFrame;
			SDL_Window* _renderWindow;
			SDL_Renderer* _renderRenderer;
			SDL_Texture* _renderTexture;
			SDL_Thread* _renderThread;

			AVRational _commonTimebase;
			AVRational _audioTimebase;
			AVRational _videoTimebase;
			long long _startTick;
		};

		static void AudioDevice(void* userdata, Uint8* stream, int len)
		{
			long long head = SFUtils::GetTickNanos();
			Desc* desc = (Desc*)userdata;
			memset(stream, 0, len);
			int sampleRate = desc->_audioStream->codecpar->sample_rate;
			short* buffer0 = ((short*)stream) + 0;
			short* buffer1 = ((short*)stream) + 1;
			int bufchs = 2;
			int bufoff = 0;
			int bufmax = len / sizeof(short);

			AVFrame*& frame = desc->_audioFrame;
			int nb_samples = 0;
			while (desc->_impl->_looping)
			{
				if (frame == NULL)
					frame = desc->_impl->_decoder.DequeueAudio();
				if (frame == NULL)
					break;
				AVSampleFormat format = (AVSampleFormat)frame->format;
				int channels = frame->channels;
				nb_samples = frame->nb_samples;
				if (sampleRate != frame->sample_rate && error(desc, -201))
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
						error(desc, -202);
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
						error(desc, -203);
						break;
					}
				}
				else
				{
					error(desc, -204);
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
				long long tick = SFUtils::GetTickMs();
				if (desc->_startTick == 0)
					desc->_startTick = tick;
				static long long last_ms0 = 0;
				long long ms0 = tick - desc->_startTick;
				long long ms1 = SFUtils::TimestampToMs(desc->_impl->_decoder.GetTimestamp(), &desc->_commonTimebase);
				//printf("nb_samples: %d\n", nb_samples);
				printf("%lld\t%lld\t%lld\t%lld\n", ms0, ms0 - last_ms0, ms1, ms0 - ms1);
				last_ms0 = ms0;

				long long samples = bufoff / bufchs;//总共消耗了frame queue多少sample
				long long shift = SFUtils::SamplesToTimestamp(samples, sampleRate, &desc->_commonTimebase);
				desc->_impl->_decoder.Forward(shift);
			}
			printf("diff: %lld\n", (SFUtils::GetTickNanos() - head));
		}

		static int RenderThread(void* param)
		{
			Desc* desc = (Desc*)param;
			AVFrame*& frame = desc->_videoFrame;
			while (desc->_impl->_looping)
			{
				SDL_Delay(15);
				if (frame == NULL)
					frame = desc->_impl->_decoder.DequeueVideo();
				if (frame == NULL)
				{
					continue;
				}
				long long timestamp = SFUtils::TimestampToTimestamp(frame->best_effort_timestamp, &desc->_videoTimebase, &desc->_commonTimebase);
				long long duration = SFUtils::TimestampToTimestamp(frame->pkt_duration, &desc->_videoTimebase, &desc->_commonTimebase);
				if (timestamp <= desc->_impl->_decoder.GetTimestamp())
				{
					if (SDL_UpdateYUVTexture(desc->_renderTexture, NULL, frame->data[0], frame->linesize[0], frame->data[1], 
						frame->linesize[1], frame->data[2], frame->linesize[2]) != 0 && error(desc, -401))
						continue;
					if (SDL_RenderCopy(desc->_renderRenderer, desc->_renderTexture, NULL, NULL) != 0 && error(desc, -402))
						continue;
					SDL_RenderPresent(desc->_renderRenderer);
					
					av_frame_unref(frame);
					av_frame_free(&frame);
					frame = NULL;
				}
				if (desc->_audioStream == NULL)
				{
					desc->_impl->_decoder.Forward(duration);
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
			return 0;
		}

		static bool error(Desc* desc, long long errorCode)
		{
			desc->_impl->_looping = false;
			desc->_impl->_errorCode = errorCode;
			return true;
		}

		static int MainThread(void* param)
		{
			SFReplayerImpl* impl = (SFReplayerImpl*)param;
			Desc desc;
			desc._impl = impl;
			desc._audioStream = NULL;
			desc._videoStream = NULL;
			desc._audioFrame = NULL;
			desc._videoFrame = NULL;
			desc._renderWindow = NULL;
			desc._renderRenderer = NULL;
			desc._renderTexture = NULL;
			desc._renderThread = NULL;
			desc._commonTimebase = impl->_decoder.GetCommonTimebase();
			desc._audioTimebase = impl->_decoder.GetAudioTimebase();
			desc._videoTimebase = impl->_decoder.GetVideoTimebase();
			desc._startTick = 0;
			if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 && error(&desc, -1))
				goto end;
			desc._audioStream = impl->_decoder.GetAudioStream();
			desc._videoStream = impl->_decoder.GetVideoStream();
			if (desc._audioStream)
			{
				SDL_AudioSpec want;
				SDL_zero(want);
				want.freq = desc._audioStream->codecpar->sample_rate;
				want.format = AUDIO_S16SYS;
				want.channels = 2;
				want.samples = impl->_decoder.GetAudioFrameSize();
				want.callback = SFReplayerImpl::AudioDevice;
				want.userdata = &desc;
				want.padding = 52428;
				SDL_AudioSpec real;
				SDL_zero(real);
				SDL_AudioDeviceID audioDeviceId = SDL_OpenAudioDevice(NULL, 0, &want, &real, 0);
				if (audioDeviceId == 0 && error(&desc, -2))
					goto end;
				SDL_PauseAudioDevice(audioDeviceId, 0);
			}
			if (desc._videoStream)
			{
				int width = desc._videoStream->codecpar[desc._videoStream->index].width;
				int height = desc._videoStream->codecpar[desc._videoStream->index].height;
				desc._renderWindow = SDL_CreateWindow("RenderWindow", 100, 100, width, height, SDL_WINDOW_SHOWN);
				if (desc._renderWindow == NULL && error(&desc, -3))
					goto end;
				desc._renderRenderer = SDL_CreateRenderer(desc._renderWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
				if (desc._renderRenderer == NULL && error(&desc, -4))
					goto end;
				desc._renderTexture = SDL_CreateTexture(desc._renderRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
				if (desc._renderTexture == NULL && error(&desc, -5))
					goto end;
				desc._renderThread = SDL_CreateThread(SFReplayerImpl::RenderThread, "RenderThread", &desc);
				if (desc._renderThread == NULL && error(&desc, -6))
					goto end;
			}

			while (desc._impl->_looping)
			{
				SDL_Event event;
				if (SDL_PollEvent(&event))
				{
					switch (event.type)
					{
					case SDL_QUIT:
						desc._impl->_looping = false;
						break;
					};
				}
				else
				{
					if (desc._impl->_decoder.GetState() == SFDecoder::Eof && desc._impl->_decoder.GetAudioQueueSize() == 0 && desc._impl->_decoder.GetVideoQueueSize() == 0)
					{
						desc._impl->_looping = false;
						break;
					}
				}
			}
		end:
			if (desc._renderThread != NULL) SDL_WaitThread(desc._renderThread, NULL);
			if (desc._renderTexture != NULL) SDL_DestroyTexture(desc._renderTexture);
			if (desc._renderRenderer != NULL) SDL_RenderClear(desc._renderRenderer);
			if (desc._renderWindow != NULL) SDL_DestroyWindow(desc._renderWindow);
			SDL_Quit();
			desc._renderThread = NULL;
			desc._renderWindow = NULL;
			desc._renderRenderer = NULL;
			desc._renderTexture = NULL;
			desc._audioStream = NULL;
			desc._videoStream = NULL;
			if (desc._audioFrame != NULL)
			{
				av_frame_unref(desc._audioFrame);
				av_frame_free(&desc._audioFrame);
			}
			if (desc._videoFrame != NULL)
			{
				av_frame_unref(desc._videoFrame);
				av_frame_free(&desc._videoFrame);
			}
			return 0;
		}
	};

	SFReplayer::SFReplayer()
	{
		_impl = NULL;
	}

	SFReplayer::~SFReplayer()
	{
		Uninit();
	}

	long long SFReplayer::Init(const char* filename, Flag flag)
	{
		Uninit();
		_impl = new(std::nothrow) SFReplayerImpl();
		if (_impl == NULL)
			return -1;
		_impl->_looping = true;
		_impl->_errorCode = 0;
		_impl->_mainThread = NULL;
		if (_impl->_decoder.Init(filename, (SFDecoder::Flag)flag) != 0 && Uninit())
			return -2;
		_impl->_mainThread = SDL_CreateThread(SFReplayerImpl::MainThread, "MainThread", _impl);
		if (_impl->_mainThread == NULL && Uninit())
			return -3;
        return 0;
	}

	bool SFReplayer::Uninit()
	{
		if (_impl != NULL)
		{
			_impl->_looping = false;
			if (_impl->_mainThread != NULL)
			{
				SDL_WaitThread(_impl->_mainThread, NULL);
				_impl->_mainThread = NULL;
			}
			delete _impl;
			_impl = NULL;
		}
		return true;
	}
}

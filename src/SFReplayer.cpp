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
	class SFReplayer::SFReplayerImpl
	{
	public:
		bool _looping;
		long long _errorCode;
		SFDecoder _decoder;
		SDL_Thread* _mainThread;

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
		};

		static void AudioDevice(void* userdata, Uint8* stream, int len)
		{
			Desc* desc = (Desc*)userdata;
			memset(stream, 0, len);
			int sampleRate = desc->_audioStream->codecpar[desc->_audioStream->index].sample_rate;
			short* buffer0 = ((short*)stream) + 0;
			short* buffer1 = ((short*)stream) + 1;
			int bufchs = 2;
			int bufoff = 0;
			int bufmax = len / sizeof(short);

			AVFrame*& frame = desc->_audioFrame;
			while (desc->_impl->_looping)
			{
				if (frame == NULL)
					frame = desc->_impl->_decoder.DequeueAudio();
				if (frame == NULL)
					break;
				AVSampleFormat format = (AVSampleFormat)frame->format;
				int channels = frame->channels;
				if ((sampleRate != frame->sample_rate || format != AV_SAMPLE_FMT_FLTP) && error(desc, -201))
					break;
				int framemax = frame->nb_samples;
				int frameoff = frame->width;
				if (channels == 6)
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
				else
				{
					desc->_impl->_errorCode = -202;
					desc->_impl->_looping = false;
					return;
				}
				frame->width = frameoff;//音频帧内的已经读取输出的偏移,单位为frame
				if (frameoff >= framemax)
				{
					av_frame_unref(frame);
					av_frame_free(&frame);
					frame = NULL;
				}
				if (bufoff >= bufmax)
					break;
			}
			long long samples = bufoff / bufchs;//总共消耗了frame queue多少sample
			long long shift = SFUtils::SamplesToTimestamp(samples, sampleRate, desc->_impl->_decoder.GetTimebase());
			desc->_impl->_decoder.Forward(shift);
		}

		static int RenderThread(void* param)
		{
			SFReplayerImpl* impl = (SFReplayerImpl*)param;
			while (impl->_looping)
			{

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
			desc._renderWindow = NULL;
			desc._renderRenderer = NULL;
			desc._renderTexture = NULL;

			if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 && error(&desc, -1))
				goto end;
			desc._audioStream = impl->_decoder.GetAudioStream();
			desc._videoStream = impl->_decoder.GetVideoStream();
			if (desc._audioStream)
			{
				SDL_AudioSpec want;
				SDL_zero(want);
				want.freq = desc._audioStream->codecpar[desc._audioStream->index].sample_rate;
				want.format = AUDIO_S16SYS;
				want.channels = 2;
				want.samples = 1024;
				want.callback = SFReplayerImpl::AudioDevice;
				want.userdata = &desc;
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
				desc._renderWindow = SDL_CreateWindow("RenderWindow", 100, 100, width / 2, height / 2, SDL_WINDOW_SHOWN);
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
						continue;
					};
				}
			}
		end:
			if (desc._renderThread != NULL) SDL_WaitThread(desc._renderThread, NULL);
			if (desc._renderTexture != NULL) SDL_DestroyTexture(desc._renderTexture);
			if (desc._renderRenderer != NULL) SDL_RenderClear(desc._renderRenderer);
			if (desc._renderWindow != NULL) SDL_DestroyWindow(desc._renderWindow);
			desc._renderThread = NULL;
			desc._audioStream = NULL;
			desc._videoStream = NULL;
			desc._renderWindow = NULL;
			desc._renderRenderer = NULL;
			desc._renderTexture = NULL;
			SDL_Quit();
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

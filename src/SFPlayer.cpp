#include "SFPlayer.h"
#include <stdio.h>
#include <new>
#include <string>
#include <vector>
#include <list>
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
	class PtrQueue
	{
	public:
		PtrQueue()
		{
			_mutex = SDL_CreateMutex();
			_list.clear();
		}

		~PtrQueue()
		{
			SDL_DestroyMutex(_mutex);
			_mutex = NULL;
			_list.clear();
		}

		int Size()
		{
			SDL_LockMutex(_mutex);
			int size = (int)_list.size();
			SDL_UnlockMutex(_mutex);
			return size;
		}

		void* PeekFront()
		{
			SDL_LockMutex(_mutex);
			void* ptr = NULL;
			if (_list.size() > 0)
				ptr = _list.front();
			SDL_UnlockMutex(_mutex);
			return ptr;
		}

		void* Dequeue()
		{
			SDL_LockMutex(_mutex);
			void* ptr = NULL;
			if (_list.size() > 0)
			{
				ptr = _list.front();
				double duration = _durations.front();
				_duration -= duration;
				if (duration < 0) duration = 0;
				_list.pop_front();
				_durations.pop_front();
			}
			SDL_UnlockMutex(_mutex);
			return ptr;
		}

		void Enqueue(void* ptr, double duration)
		{
			SDL_LockMutex(_mutex);
			_list.push_back(ptr);
			_durations.push_back(duration);
			_duration += duration;
			SDL_UnlockMutex(_mutex);
		}

		double GetDuration()
		{
			SDL_LockMutex(_mutex);
			double duration = _duration;
			SDL_UnlockMutex(_mutex);
			return duration;
		}

		SDL_mutex* _mutex;
		std::list<void*> _list;
		std::list<double> _durations;
		double _duration;
	};

	class SFPlayer::SFPlayerImpl
	{
	public:
		std::string _filename;
		bool _videoEnabled;
		bool _audioEnabled;
		SDL_Thread* _mainthread;
		SDL_Window* _window;
		SDL_Renderer* _renderer;
		SDL_Texture* _texture;
		bool _looping;
		long long _errorCode;
		std::vector<SDL_Thread*> _threads;

		AVFormatContext* _pFormatCtx;
		int _videoStreamIndex;
		int _audioStreamIndex;
		AVCodecContext* _pVideoCodecCtx;
		AVCodecContext* _pAudioCodecCtx;
		PtrQueue _videoPackets;
		PtrQueue _audioPackets;
		PtrQueue _videoFrames;
		PtrQueue _audioFrames;
		AVSampleFormat _audioSampleFormat;
		int _audioSampleRate;
		int _audioChannels;

		long long _playSampleOffset;
		long long _playSampleCount;
		long long _playSampleRate;
		double _playTimeGlobal;

		static void AudioDevice(void* userdata, Uint8* stream, int len)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)userdata;
			short* buffer0 = ((short*)stream) + 0;
			short* buffer1 = ((short*)stream) + 1;
			int bufchs = 2;
			int bufoff = 0;
			int bufmax = len / sizeof(short);
			while(true)
			{
				AVFrame* frame = (AVFrame*)impl->_audioFrames.PeekFront();
				if (frame == NULL) 
					break;
				AVSampleFormat format = (AVSampleFormat)frame->format;
				int sampleRate = frame->sample_rate;
				int channels = frame->channels;
				if (sampleRate != impl->_audioSampleRate)
				{
					impl->_errorCode = -201;
					impl->_looping = false;
					return;
				}
				if (format == AV_SAMPLE_FMT_FLTP)
				{
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
						impl->_errorCode = -202;
						impl->_looping = false;
						return;
					}
					frame->width = frameoff;
					if (frameoff >= framemax)
					{
						frame = (AVFrame*)impl->_audioFrames.Dequeue();
						av_frame_unref(frame);
						av_frame_free(&frame);
						frame = NULL;
					}
					if (bufoff >= bufmax)
					{
						break;
					}
				}
				else
				{
					impl->_errorCode = -203;
					impl->_looping = false;
					return;
				}
			}


			impl->_playSampleCount += bufmax / bufchs;
			impl->_playTimeGlobal = (impl->_playSampleOffset + impl->_playSampleCount) / (double)impl->_playSampleRate;
			static int ind = 0;
			printf("Audio[%d]: %lld + %lld, %fs\n", ind++, impl->_playSampleOffset, impl->_playSampleCount, impl->_playTimeGlobal);
		}

		static int AudioThread(void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			impl->_playSampleOffset = 0;
			impl->_playSampleCount = 0;
			impl->_playSampleRate = impl->_pAudioCodecCtx->sample_rate;
			impl->_playTimeGlobal = 0.0;

			AVFrame* frame = NULL;
			while (impl->_looping)
			{
				AVPacket* packet = (AVPacket*)impl->_audioPackets.Dequeue();
				if (packet == NULL)
					continue;
				int ret = avcodec_send_packet(impl->_pAudioCodecCtx, packet);
				av_packet_unref(packet);
				av_packet_free(&packet);
				packet = NULL;
				if (ret != 0)
				{
					impl->_errorCode = -21;
					impl->_looping = false;
					break;
				}
				while (true)
				{
					if (frame == NULL)
					{
						frame = av_frame_alloc();
						if (frame == NULL)
						{
							impl->_errorCode = -22;
							impl->_looping = false;
							break;
						}
					}
					ret = avcodec_receive_frame(impl->_pAudioCodecCtx, frame);
					if (ret < 0)
					{
						if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
						{
							impl->_errorCode = -23;
							impl->_looping = false;
						}
						break;
					}
					//static int ind = 0;
					//AVStream* stream = impl->_pFormatCtx->streams[impl->_audioStreamIndex];
					//AVRational time_base = stream->time_base;
					//printf("[%d] Audio pts:%lld dts:%lld nb_samples:%d timebase=%d/%d\n", ind++, frame->pts, frame->pkt_dts, frame->nb_samples, time_base.num, time_base.den);
					double duration = frame->nb_samples / (double)frame->sample_rate;
					impl->_audioFrames.Enqueue(frame, duration);
					frame = NULL;
				}
			}
			if (frame != NULL)
			{
				av_frame_free(&frame);
				av_free(frame);
				frame = NULL;
			}
			return 0;
		}
		
		static int VideoThread(void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			AVFrame* frame = NULL;
			while (impl->_looping)
			{
				AVPacket* packet = (AVPacket*)impl->_videoPackets.Dequeue();
				if (packet == NULL)
					continue;
				int ret = avcodec_send_packet(impl->_pVideoCodecCtx, packet);
				av_packet_unref(packet);
				av_packet_free(&packet);
				packet = NULL;
				if (ret != 0)
				{
					impl->_errorCode = -21;
					impl->_looping = false;
					break;
				}
				while (true)
				{
					if (frame == NULL)
					{
						frame = av_frame_alloc();
						if (frame == NULL)
						{
							impl->_errorCode = -22;
							impl->_looping = false;
							break;
						}
					}
					int ret = avcodec_receive_frame(impl->_pVideoCodecCtx, frame);
					if (ret < 0)
					{
						if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
						{
							impl->_errorCode = -23;
							impl->_looping = false;
						}
						break;
					}
					static int ind = 0;
					AVStream* stream = impl->_pFormatCtx->streams[impl->_videoStreamIndex];
					AVRational time_base = stream->time_base;
					printf("[%d] Frame %c (%d) pts:%lld dts:%lld key_frame:%d [coded_picture_number:%d, display_picture_number:%d, %dx%d] timebase=%d/%d\n",
						ind++,
						av_get_picture_type_char(frame->pict_type),
						0,//videoState->video_ctx->frame_number,
						frame->pts,
						frame->pkt_dts,
						frame->key_frame,
						frame->coded_picture_number,
						frame->display_picture_number,
						frame->width,
						frame->height,
						time_base.num,
						time_base.den);
					frame->pkt_duration
					impl->_videoFrames.Enqueue(frame, 0);
					frame = NULL;
				}
			}
			if (frame != NULL)
			{
				av_frame_free(&frame);
				av_free(frame);
				frame = NULL;
			}
			return 0;
		}

#define ERROR_END(code) {impl->_errorCode = code; goto end;}

		static int MainThread(void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			if (avformat_open_input(&impl->_pFormatCtx, impl->_filename.c_str(), NULL, NULL) != 0)
				ERROR_END(-4);
			if (avformat_find_stream_info(impl->_pFormatCtx, NULL) < 0)
				ERROR_END(-5);
			for (int i = 0; i < (int)impl->_pFormatCtx->nb_streams; i++)
			{
				if (impl->_videoStreamIndex == -1 && impl->_pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
					impl->_videoStreamIndex = i;
				if (impl->_audioStreamIndex == -1 && impl->_pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
					impl->_audioStreamIndex = i;
			}
			if (impl->_videoStreamIndex == -1 && impl->_audioStreamIndex == -1)
				ERROR_END(-6);
			if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
				ERROR_END(-1);
			if (impl->_videoStreamIndex >= 0 && impl->_videoEnabled)
			{
				AVCodec* pVideoCodec = avcodec_find_decoder(impl->_pFormatCtx->streams[impl->_videoStreamIndex]->codecpar->codec_id);
				if (pVideoCodec == NULL)
					ERROR_END(-7);
				impl->_pVideoCodecCtx = avcodec_alloc_context3(pVideoCodec);
				if (avcodec_parameters_to_context(impl->_pVideoCodecCtx, impl->_pFormatCtx->streams[impl->_videoStreamIndex]->codecpar) != 0)
					ERROR_END(-8);
				if (avcodec_open2(impl->_pVideoCodecCtx, pVideoCodec, NULL) != 0)
					ERROR_END(-9);
				int width = impl->_pVideoCodecCtx->width;
				int height = impl->_pVideoCodecCtx->height;
				impl->_window = SDL_CreateWindow("MainWindow", 100, 100, width/2, height/2, SDL_WINDOW_SHOWN);
				if (impl->_window == NULL)
					ERROR_END(-2);
				impl->_renderer = SDL_CreateRenderer(impl->_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
				if (impl->_renderer == NULL)
					ERROR_END(-3);
				impl->_texture = SDL_CreateTexture(impl->_renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
				if (impl->_texture == NULL)
					ERROR_END(-4);
				SDL_Thread* videoThread = SDL_CreateThread(VideoThread, "VideoThread", impl);
				if (videoThread == NULL)
				{
					impl->_errorCode = -10;
					goto end;
				}
				impl->_threads.push_back(videoThread);
			}
			if (impl->_audioStreamIndex >= 0 && impl->_audioEnabled)
			{
				AVCodec* pAudioCodec = avcodec_find_decoder(impl->_pFormatCtx->streams[impl->_audioStreamIndex]->codecpar->codec_id);
				if (pAudioCodec == NULL) 
					ERROR_END(-11);
				impl->_pAudioCodecCtx = avcodec_alloc_context3(pAudioCodec);
				if (avcodec_parameters_to_context(impl->_pAudioCodecCtx, impl->_pFormatCtx->streams[impl->_audioStreamIndex]->codecpar) != 0)
					ERROR_END(-8);
				if (avcodec_open2(impl->_pAudioCodecCtx, pAudioCodec, NULL) != 0)
					ERROR_END(-9);
				impl->_audioSampleFormat = impl->_pAudioCodecCtx->sample_fmt;
				impl->_audioSampleRate = impl->_pAudioCodecCtx->sample_rate;
				impl->_audioChannels = impl->_pAudioCodecCtx->channels;
				SDL_AudioSpec want;
				SDL_zero(want);
				want.freq = impl->_audioSampleRate;
				want.format = AUDIO_S16SYS;
				want.channels = 2;
				want.samples = 1024;
				want.callback = AudioDevice;
				want.userdata = impl;
				SDL_AudioSpec real;
				SDL_zero(real);
				SDL_AudioDeviceID audioDeviceID = SDL_OpenAudioDevice(NULL, 0, &want, &real, 0);
				if (audioDeviceID == 0)
					ERROR_END(-11);
				SDL_PauseAudioDevice(audioDeviceID, 0);
				SDL_Thread* audioThread = SDL_CreateThread(AudioThread, "AudioThread", impl);
				if (audioThread == NULL)
					ERROR_END(-11);
				impl->_threads.push_back(audioThread);
			}
			
			while (impl->_looping)
			{
				SDL_Event event;
				if (SDL_PollEvent(&event))
				{
					switch (event.type)
					{
					case SDL_QUIT:
						impl->_looping = false;
						break;
					};
				}
				else if (impl->_videoStreamIndex >= 0 && impl->_videoFrames.Size() > 0)
				{//video render
					AVFrame* frame = (AVFrame*)impl->_videoFrames.Dequeue();
					if (frame != NULL)
					{
						if (SDL_UpdateYUVTexture(impl->_texture, NULL,
							frame->data[0], frame->linesize[0],
							frame->data[1], frame->linesize[1],
							frame->data[2], frame->linesize[2]) != 0)
						{
							impl->_errorCode = -11;
							goto end;
						}
						SDL_RenderCopy(impl->_renderer, impl->_texture, NULL, NULL);
						SDL_RenderPresent(impl->_renderer);
						SDL_Delay(20);
						av_frame_unref(frame);
						av_frame_free(&frame);
						frame = NULL;
					}
				}
				else if ((impl->_videoEnabled && impl->_videoPackets.Size() < 50) || (impl->_audioEnabled && impl->_audioPackets.Size() < 50))
				{//demux
					AVPacket* packet = av_packet_alloc();
					if (packet == NULL)
					{
						impl->_errorCode = -11;
						goto end;
					}
					if (av_read_frame(impl->_pFormatCtx, packet) != 0)
					{
						impl->_errorCode = -12;
						goto end;
					}
					if (impl->_videoEnabled && packet->stream_index == impl->_videoStreamIndex)
					{
						impl->_videoPackets.Enqueue(packet, 0);
					}
					else if (impl->_audioEnabled && packet->stream_index == impl->_audioStreamIndex)
					{
						impl->_audioPackets.Enqueue(packet, 0);
					}
					else
					{
						av_packet_unref(packet);
						av_packet_free(&packet);
						packet = NULL;
					}
				}
			}

		end:
			for (int i = 0; i < (int)impl->_threads.size(); i++)
			{
				SDL_WaitThread(impl->_threads[i], NULL);
			}
			for (AVPacket* packet = (AVPacket*)impl->_videoPackets.Dequeue(); packet != NULL; packet = (AVPacket*)impl->_videoPackets.Dequeue())
			{
				av_packet_unref(packet);
				av_packet_free(&packet);
			}
			for (AVPacket* packet = (AVPacket*)impl->_audioPackets.Dequeue(); packet != NULL; packet = (AVPacket*)impl->_audioPackets.Dequeue())
			{
				av_packet_unref(packet);
				av_packet_free(&packet);
			}
			if (impl->_pAudioCodecCtx != NULL)
			{
				avcodec_close(impl->_pAudioCodecCtx);
				impl->_pAudioCodecCtx = NULL;
			}
			if (impl->_pVideoCodecCtx != NULL)
			{
				avcodec_close(impl->_pVideoCodecCtx);
				impl->_pVideoCodecCtx = NULL;
			}
			impl->_audioStreamIndex = -1;
			impl->_videoStreamIndex = -1;
			if (impl->_pFormatCtx != NULL)
			{
				avformat_close_input(&impl->_pFormatCtx);
				impl->_pFormatCtx = NULL;
			}
			if (impl->_texture != NULL)
			{
				SDL_DestroyTexture(impl->_texture);
				impl->_texture = NULL;
			}
			if (impl->_renderer != NULL)
			{
				SDL_RenderClear(impl->_renderer);
				impl->_renderer = NULL;
			}
			if (impl->_window != NULL)
			{
				SDL_DestroyWindow(impl->_window);
				impl->_window = NULL;
			}
			SDL_Quit();
			return 0;
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

	long long SFPlayer::Init(const char* filename, bool videoEnabled, bool audioEnabled)
	{
		Uninit();
		_impl = new(std::nothrow) SFPlayerImpl();
		if (_impl == NULL)
			return -1;
		_impl->_filename = filename;
		_impl->_videoEnabled = videoEnabled;
		_impl->_audioEnabled = audioEnabled;
		_impl->_mainthread = NULL;
		_impl->_window = NULL;
		_impl->_renderer = NULL;
		_impl->_texture = NULL;
		_impl->_looping = true;
		_impl->_errorCode = 0;
		_impl->_pFormatCtx = NULL;
		_impl->_videoStreamIndex = -1;
		_impl->_audioStreamIndex = -1;
		_impl->_pVideoCodecCtx = NULL;
		_impl->_pAudioCodecCtx = NULL;
		_impl->_audioSampleFormat = (AVSampleFormat)0;
		_impl->_audioSampleRate = 0;
		_impl->_audioChannels = 0;
		_impl->_playSampleOffset = 0;
		_impl->_playSampleCount = 0;
		_impl->_playSampleRate = 0;
		_impl->_playTimeGlobal = 0;
		_impl->_mainthread = SDL_CreateThread(SFPlayerImpl::MainThread, "SFPlayer::MainThread", _impl);
		if (_impl->_mainthread == NULL)
		{
			Uninit();
			return -2;
		}
		return 0;
	}

	void SFPlayer::Uninit()
	{
		if (_impl != NULL)
		{
			_impl->_looping = false;
			if (_impl->_mainthread != NULL)
			{
				SDL_WaitThread(_impl->_mainthread, NULL);
				_impl->_mainthread = NULL;
			}
			delete _impl;
			_impl = NULL;
		}
	}
}

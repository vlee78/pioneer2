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

		void* Dequeue()
		{
			SDL_LockMutex(_mutex);
			void* ptr = NULL;
			if (_list.size() > 0)
			{
				ptr = _list.front();
				_list.pop_front();
			}
			SDL_UnlockMutex(_mutex);
			return ptr;
		}

		void Enqueue(void* ptr)
		{
			SDL_LockMutex(_mutex);
			_list.push_back(ptr);
			SDL_UnlockMutex(_mutex);
		}

		SDL_mutex* _mutex;
		std::list<void*> _list;
	};

	class SFPlayer::SFPlayerImpl
	{
	public:
		std::string _filename;
		SDL_Thread* _mainthread;
		SDL_Window* _window;
		SDL_Renderer* _renderer;
		bool _looping;
		long long _errorCode;
		std::vector<SDL_Thread*> _threads;

		AVFormatContext* _pFormatCtx;
		int _videoStreamIndex;
		int _audioStreamIndex;
		AVCodecContext* _pVideoCodecCtx;
		PtrQueue _videoPackets;
		PtrQueue _audioPackets;

		static int VideoThread(void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;

			while (impl->_looping)
			{
				AVPacket* packet = (AVPacket*)impl->_videoPackets.Dequeue();
				if (packet == NULL)
					continue;

				if (avcodec_send_packet(impl->_pVideoCodecCtx, packet) != 0)
				{
					av_packet_unref(packet);
					av_packet_free(&packet);
					packet = NULL;
					impl->_errorCode = -21;
					impl->_looping = false;
					break;
				}

				AVFrame* frame = av_frame_alloc();
				if (frame == NULL)
				{
					av_packet_unref(packet);
					av_packet_free(&packet);
					packet = NULL;
					impl->_errorCode = -22;
					impl->_looping = false;
					break;
				}
				int ret = 0;
				while (ret >= 0)
				{
					ret = avcodec_receive_frame(impl->_pVideoCodecCtx, frame);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					{
						av_frame_free(&frame);
						av_free(frame);
						frame = NULL;
						break;
					}
					else if (ret < 0)
					{
						av_frame_free(&frame);
						av_free(frame);
						frame = NULL;
						av_packet_unref(packet);
						av_packet_free(&packet);
						packet = NULL;
						impl->_errorCode = -22;
						impl->_looping = false;
						break;
					}
					else
					{
						//
					}
				}
			}
			

			/*
			SFPlayerImpl* impl = (SFPlayerImpl*)param;


			AVPacket* pVideoPacket = av_packet_alloc();
			AVFrame* pVideoFrame = av_frame_alloc();

			while (true)
			{
				avcodec_send_packet(impl->_pVideoCodecCtx)
			}


			
			SDL_Surface* surface = SDL_LoadBMP("Penguins.bmp");
			SDL_Texture* texture = SDL_CreateTextureFromSurface(impl->_renderer, surface);
			SDL_FreeSurface(surface);
			surface = NULL;
			while (impl->_looping)
			{
				SDL_RenderClear(impl->_renderer);
				SDL_RenderCopy(impl->_renderer, texture, NULL, NULL);
				SDL_RenderPresent(impl->_renderer);
			}
			SDL_DestroyTexture(texture);
			texture = NULL;
			*/
			return 0;
		}

		static int MainThread(void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
			{
				impl->_errorCode = -1;
				goto end;
			}
			impl->_window = SDL_CreateWindow("MainWindow", 100, 100, 400, 400, SDL_WINDOW_SHOWN);
			if (impl->_window == NULL)
			{
				impl->_errorCode = -2;
				goto end;
			}
			impl->_renderer = SDL_CreateRenderer(impl->_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
			if (impl->_renderer == NULL)
			{
				impl->_errorCode = -3;
				goto end;
			}


			if (avformat_open_input(&impl->_pFormatCtx, impl->_filename.c_str(), NULL, NULL) != 0)
			{
				impl->_errorCode = -4;
				goto end;
			}
			if (avformat_find_stream_info(impl->_pFormatCtx, NULL) < 0)
			{
				impl->_errorCode = -5;
				goto end;
			}
			for (int i = 0; i < (int)impl->_pFormatCtx->nb_streams; i++)
			{
				if (impl->_videoStreamIndex == -1 && impl->_pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
					impl->_videoStreamIndex = i;
				if (impl->_audioStreamIndex == -1 && impl->_pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
					impl->_audioStreamIndex = i;
			}
			if (impl->_videoStreamIndex == -1 || impl->_audioStreamIndex == -1)
			{
				impl->_errorCode = -6;
				goto end;
			}
			AVCodec* pVideoCodec = avcodec_find_decoder(impl->_pFormatCtx->streams[impl->_videoStreamIndex]->codecpar->codec_id);
			if (pVideoCodec == NULL)
			{
				impl->_errorCode = -7;
				goto end;
			}
			impl->_pVideoCodecCtx = avcodec_alloc_context3(pVideoCodec);
			if (avcodec_parameters_to_context(impl->_pVideoCodecCtx, impl->_pFormatCtx->streams[impl->_videoStreamIndex]->codecpar) != 0)
			{
				impl->_errorCode = -8;
				goto end;
			}
			if (avcodec_open2(impl->_pVideoCodecCtx, pVideoCodec, NULL) != 0)
			{
				impl->_errorCode = -9;
				goto end;
			}



			SDL_Thread* thread = SDL_CreateThread(VideoThread, "VideoThread", impl);
			if (thread == NULL)
			{
				impl->_errorCode = -10;
				goto end;
			}
			impl->_threads.push_back(thread);

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
				else if (impl->_videoPackets.Size() < 50 && impl->_audioPackets.Size() < 50)
				{
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
					if (packet->stream_index == impl->_videoStreamIndex)
					{
						impl->_videoPackets.Enqueue(packet);
					}
					else if (packet->stream_index == impl->_audioStreamIndex)
					{
						impl->_audioPackets.Enqueue(packet);
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

	long long SFPlayer::Init(const char* filename)
	{
		Uninit();
		_impl = new(std::nothrow) SFPlayerImpl();
		if (_impl == NULL)
			return -1;
		_impl->_filename = filename;
		_impl->_mainthread = NULL;
		_impl->_window = NULL;
		_impl->_renderer = NULL;
		_impl->_looping = true;
		_impl->_errorCode = 0;
		_impl->_pFormatCtx = NULL;
		_impl->_videoStreamIndex = -1;
		_impl->_audioStreamIndex = -1;
		_impl->_pVideoCodecCtx = NULL;
		
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

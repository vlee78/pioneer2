#include "SFPlayer.h"
#include <stdio.h>
#include <new>
#include <string>
#include <vector>
#include "SDL.h"

namespace pioneer
{
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

		static int SubThread(void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			SDL_Surface* surface = SDL_LoadBMP("Penguins.bmp");
			SDL_Texture* texture = SDL_CreateTextureFromSurface(impl->_renderer, surface);
			SDL_FreeSurface(surface);
			surface = NULL;

			//for (int i = 0; i < 20; i++)
			//{
				SDL_RenderClear(impl->_renderer);
				SDL_RenderCopy(impl->_renderer, texture, NULL, NULL);
				SDL_RenderPresent(impl->_renderer);
			//	SDL_Delay(1000);
			//}

			SDL_DestroyTexture(texture);
			texture = NULL;
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
			impl->_window = SDL_CreateWindow("MainWindow", 100, 100, 100, 100, SDL_WINDOW_SHOWN);
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

			SDL_Thread* thread = SDL_CreateThread(SubThread, "SubThread", impl);
			if (thread == NULL)
			{
				impl->_errorCode = -4;
				goto end;
			}
			impl->_threads.push_back(thread);
			impl->_looping = true;
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
			}
		end:
			for (int i = 0; i < (int)impl->_threads.size(); i++)
			{
				SDL_WaitThread(impl->_threads[i], NULL);
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
		_impl->_looping = false;
		_impl->_errorCode = 0;
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

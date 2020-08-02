#include "Pioneer2.h"
#include "SDL.h"
#include <stdlib.h>
#include <string.h>
#include <string>

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
#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

	struct State
	{
		std::string filepath;
		int maxFramesToDecode;
		SDL_mutex* pictq_mutext;
		SDL_cond* pictq_cond;
		bool quit;

		~State()
		{

		}
	};

	static int decode_thread(void* context)
	{//decode线程,直到达到maxDecodeFrame,或者decode出错
		State* state = (State*)context;
		const char* url = state->filepath.c_str();
		AVFormatContext* pFormatCtx = NULL;
		if (avformat_open_input(&pFormatCtx, url, NULL, NULL) < 0)
		{

		}

		return 0;
	}

	static unsigned int refresh_callback(unsigned int interval, void* context)
	{
		SDL_Event event;
		event.type = FF_REFRESH_EVENT;
		event.user.data1 = context;
		SDL_PushEvent(&event);
		return 0;
	}

	static void schedule_refresh(State* state, int delayMs)
	{
		SDL_AddTimer(delayMs, refresh_callback, state);
	}

	static void video_refresh_timer(State* state)
	{

	}

	int Pioneer2::testPioneer2(int argc, const char* argv[])
	{
		if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) 
			return -1;

		State state;
		state.filepath = "test.mov";
		state.maxFramesToDecode = 1024;
		state.pictq_mutext = SDL_CreateMutex();
		state.pictq_cond = SDL_CreateCond();
		state.quit = false;

		if (SDL_CreateThread(decode_thread, "decode_thread", &state) == 0)
			return -2;

		schedule_refresh(&state, 39);

		SDL_Event event;
		while (true)
		{
			SDL_WaitEvent(&event);
			switch (event.type)
			{
			case FF_QUIT_EVENT:
			case SDL_QUIT:
				state.quit = true;
				SDL_Quit();
				break;
			case FF_REFRESH_EVENT:
				video_refresh_timer(&state);
				break;
			default:
				break;
			}

			if (state.quit)
				break;
		}

		return 0;
	}
}

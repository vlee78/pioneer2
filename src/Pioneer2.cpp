#include "Pioneer2.h"
#include "SDL.h"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <list>

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

		SDL_mutex* videoPacketsMutex;
		SDL_mutex* videoFramesMutex;
		std::list<AVPacket*> videoPacketsQueue;
		std::list<AVFrame*> videoFramesQueue;
		AVCodecContext* videoCodecCtx;
		SDL_Thread* videoDecodeThread;
		SwsContext * swsCtx;

		SDL_Window* sdlWindow;
		SDL_Renderer* sdlRenderer;
		SDL_Texture* sdlTexture;

		int screenFormat;
		int screenWidth;
		int screenHeight;
		int windowWidth;
		int windowHeight;

		~State()
		{

		}
	};

	//视频解码线程
	int video_thread(void * arg)
	{
		State* state = (State*)arg;
		AVCodecContext* pCodecCtx = state->videoCodecCtx;

		//视频解码循环，公用一个packet
		AVPacket* pPacket = av_packet_alloc();
		if (pPacket == NULL)
			return -1;

		AVFrame* pFrame = av_frame_alloc();
		if (pFrame == NULL)
			return -2;
		
		while (true)
		{
			SDL_LockMutex(state->videoFramesMutex);
			int size = (int)state->videoFramesQueue.size();
			SDL_UnlockMutex(state->videoFramesMutex);
			if (size >= 1)
			{
				SDL_Delay(10);
				continue;
			}

			AVPacket* packet = NULL;
			SDL_LockMutex(state->videoPacketsMutex);
			if (state->videoPacketsQueue.size() > 0)
			{
				packet = state->videoPacketsQueue.front();
				state->videoPacketsQueue.pop_front();
			}
			SDL_UnlockMutex(state->videoPacketsMutex);
			if (packet == NULL)
			{
				SDL_Delay(10);
				continue;
			}

			if (avcodec_send_packet(pCodecCtx, packet) < 0)
				return -3;
			av_packet_unref(packet);
			av_packet_free(&packet);


			//反复输出frame直至需要更多输入为止
			while (true)
			{
				int ret = avcodec_receive_frame(pCodecCtx, pFrame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					break;//需要更多输入或者读到尾了
				else if (ret < 0)
					return -4;

				//解压出一个frame
				AVFrame* frame = av_frame_alloc();
				if (frame == NULL)
					return -5;
				if (av_frame_ref(frame, pFrame) < 0)
					return -6;
				SDL_LockMutex(state->videoPacketsMutex);
				state->videoFramesQueue.push_back(frame);
				SDL_UnlockMutex(state->videoPacketsMutex);
			}
			av_packet_unref(pPacket);
		}
		av_frame_free(&pFrame);
		av_packet_free(&pPacket);
	}

	static int decode_thread(void* context)
	{//decode线程,直到达到maxDecodeFrame,或者decode出错
		State* state = (State*)context;
		const char* url = state->filepath.c_str();
		AVFormatContext* pFormatCtx = NULL;
		if (avformat_open_input(&pFormatCtx, url, NULL, NULL) < 0)
			return -1;
		
		//查找streamId
		if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
			return -2;

		int videoStream = -1;
		for (int i = 0; i < (int)pFormatCtx->nb_streams; i++)
		{
			if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				videoStream = i;
				break;
			}
		}
		if (videoStream < 0)
			return -3;

		//打开codec编码器
		AVCodec* pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
		if (pCodec == NULL)
			return -4;

		//打开codec context
		AVCodecContext* pCodecCtx = avcodec_alloc_context3(pCodec);
		if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar) < 0)
			return -5;
		if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
			return -6;

		state->videoCodecCtx = pCodecCtx;
		state->videoDecodeThread = SDL_CreateThread(video_thread, "Video Thread", state);

		int width = pCodecCtx->width;
		int height = pCodecCtx->height;

		state->sdlWindow = SDL_CreateWindow(
			"FFmpeg SDL Video Player",
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			state->windowWidth,
			state->windowHeight,
			SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI
		);
		if (state->sdlWindow == NULL)
			return -1;
		SDL_GL_SetSwapInterval(1);

		//state->sdlRenderer = SDL_CreateRenderer(state->sdlScreen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
		state->sdlRenderer = SDL_CreateRenderer(state->sdlWindow, -1, 0);

		state->sdlTexture = SDL_CreateTexture(
			state->sdlRenderer,
			SDL_PIXELFORMAT_YV12,
			SDL_TEXTUREACCESS_STREAMING,
			1440/2,
			900/2
		);


		// set up the VideoState SWSContext to convert the image data to YUV420
		state->swsCtx = sws_getContext(state->videoCodecCtx->width,
			state->videoCodecCtx->height,
			state->videoCodecCtx->pix_fmt,
			1440/2,
			900/2,
			AV_PIX_FMT_YUV420P,
			SWS_BILINEAR,
			NULL,
			NULL,
			NULL
		);


		AVPacket* pPacket = av_packet_alloc();
		if (pPacket == NULL)
			return -7;
		while (true)
		{
			if (state->quit)
				break;

			SDL_LockMutex(state->videoPacketsMutex);
			int size = (int)state->videoPacketsQueue.size();
			SDL_UnlockMutex(state->videoPacketsMutex);
			if (size > 0)
			{
				SDL_Delay(100);
				continue;
			}

			if (av_read_frame(pFormatCtx, pPacket) < 0)
				return -7;
			if (pPacket->stream_index == videoStream)
			{
				AVPacket* packet = av_packet_alloc();
				if (av_packet_ref(packet, pPacket) < 0)
					return -8;
				SDL_LockMutex(state->videoPacketsMutex);
				state->videoPacketsQueue.push_back(packet);
				SDL_UnlockMutex(state->videoPacketsMutex);
			}
			av_packet_unref(pPacket);
		}
		av_packet_free(&pPacket);
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
		schedule_refresh(state, 39);
		AVFrame* frame = NULL;
		SDL_LockMutex(state->videoFramesMutex);
		if (state->videoFramesQueue.size() > 0)
		{
			frame = state->videoFramesQueue.front();
			state->videoFramesQueue.pop_front();
		}
		SDL_UnlockMutex(state->videoFramesMutex);
		if (frame == NULL)
			return;

		int width = state->videoCodecCtx->width;
		int height = state->videoCodecCtx->height;

		static AVFrame* pFrameRgb = NULL;
		if (pFrameRgb == NULL)
		{
			pFrameRgb = av_frame_alloc();
			if (pFrameRgb == NULL)
				return;
			int buflen = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, 1440/2, 900/2, 32);
			uint8_t* buffer = (uint8_t*)av_malloc(buflen);

			av_image_fill_arrays(pFrameRgb->data,
				pFrameRgb->linesize,
				buffer,
				AV_PIX_FMT_YUV420P, 
				1440 / 2, 
				900 / 2, 
				32);
		}
		
		int reth = sws_scale(state->swsCtx, frame->data, frame->linesize, 0, height, pFrameRgb->data, pFrameRgb->linesize);
		if (reth < 0)
			return;

		if (state->videoCodecCtx->pix_fmt != AV_PIX_FMT_YUV420P)
			return;
		SDL_Rect rect;
		rect.x = 0;
		rect.y = 0;
		rect.w = 1440/2;
		rect.h = 900/2;
		int ret = SDL_UpdateYUVTexture(
			state->sdlTexture,
			&rect,
			pFrameRgb->data[0],
			pFrameRgb->linesize[0],
			pFrameRgb->data[1],
			pFrameRgb->linesize[1],
			pFrameRgb->data[2],
			pFrameRgb->linesize[2]
		);
		if (ret < 0)
			return;

		//av_frame_unref(pFrameRgb);


		SDL_RenderClear(state->sdlRenderer);

		SDL_RenderCopy(state->sdlRenderer, state->sdlTexture, NULL, &rect);

		SDL_RenderPresent(state->sdlRenderer);

		av_frame_unref(frame);
		av_frame_free(&frame);
	}

	int Pioneer2::testPioneer2(int argc, const char* argv[])
	{
		if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) 
			return -1;



		SDL_DisplayMode dm;
		if (SDL_GetDesktopDisplayMode(0, &dm) != 0)
			return 1;

		int w, h;
		w = dm.w;
		h = dm.h;



		State state;
		state.filepath = "test.mov";
		state.maxFramesToDecode = 1024;
		state.pictq_mutext = SDL_CreateMutex();
		state.pictq_cond = SDL_CreateCond();
		state.quit = false;
		state.videoPacketsMutex = SDL_CreateMutex();
		state.videoFramesMutex = SDL_CreateMutex();
		state.sdlWindow = NULL;
		state.sdlRenderer = NULL;
		state.sdlTexture = NULL;

		state.screenFormat = dm.format;
		state.screenWidth = dm.w;
		state.screenHeight = dm.h;
		state.windowWidth = dm.w / 2;
		state.windowHeight = dm.h / 2;
		
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

		SDL_DestroyWindow(state.sdlWindow);
		SDL_Quit();
		return 0;
	}
}

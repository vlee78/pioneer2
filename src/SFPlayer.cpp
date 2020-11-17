#include "SFPlayer.h"
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
	class PtrQueue
	{
	public:
		PtrQueue()
		{
			_mutex = SDL_CreateMutex();
			_list.clear();
			_durations.clear();
			_duration = 0;
		}

		~PtrQueue()
		{
			SDL_DestroyMutex(_mutex);
			_mutex = NULL;
			_list.clear();
			_durations.clear();
			_duration = 0;
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
        struct AudioDesc
        {
            bool* _looping;
			State* _state;
			long long* _errorCode;
            double* _time;
            long long* _ts;
            PtrQueue* _audioPackets;
            PtrQueue* _audioFrames;
            AVCodecContext* _audioCodecCtx;
        };
        
		static void log(const char* format, ...)
		{
			static FILE* file = NULL;
			if (file == NULL)
				file = fopen("log.txt", "wb");
			va_list list;
			va_start(list, format);
			vfprintf(file, format, list);
			va_end(list);
			fflush(file);

			va_list list2;
			va_start(list2, format);
			vprintf(format, list2);
			va_end(list2);
			
		}

        static void AudioDevice(void* userdata, Uint8* stream, int len)
        {
            AudioDesc* desc = (AudioDesc*)userdata;
			memset(stream, 0, len);
			if (*desc->_looping == false || *desc->_state != Playing)
				return;
            short* buffer0 = ((short*)stream) + 0;
            short* buffer1 = ((short*)stream) + 1;
            int bufchs = 2;
            int bufoff = 0;
            int bufmax = len / sizeof(short);
            while(true)
            {
                AVFrame* frame = (AVFrame*)desc->_audioFrames->PeekFront();
                if (frame == NULL)
                    break;
                AVSampleFormat format = (AVSampleFormat)frame->format;
                int sampleRate = frame->sample_rate;
                int channels = frame->channels;
                if (sampleRate != desc->_audioCodecCtx->sample_rate)
                {
                    *desc->_errorCode = -201;
                    *desc->_looping = false;
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
                        *desc->_errorCode = -202;
                        *desc->_looping = false;
                        return;
                    }
                    frame->width = frameoff;
                    if (frameoff >= framemax)
                    {
                        frame = (AVFrame*)desc->_audioFrames->Dequeue();
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
                    *desc->_errorCode = -203;
                    *desc->_looping = false;
                    return;
                }
            }

            int samples = bufmax / bufchs;
            double shift = samples / (double)desc->_audioCodecCtx->sample_rate;
            *desc->_time += shift;
            *desc->_ts = 0;
            //printf("Audio[%d]: %lld + %lld, %fs\n", ind++, impl->_playSampleOffset, impl->_playSampleCount, impl->_playTimeGlobal);
          
        }
        
        static int AudioThread(void* param)
        {
            AudioDesc* desc = (AudioDesc*)param;
            AVFrame* frame = NULL;
            while (*desc->_looping)
            {
				if (desc->_audioFrames->GetDuration() > 2.0)
				{
					SDL_Delay(100);
					continue;
				}

                AVPacket* packet = (AVPacket*)desc->_audioPackets->Dequeue();
                if (packet == NULL)
                {
                    SDL_Delay(100);
                    continue;
                }
                int ret = avcodec_send_packet(desc->_audioCodecCtx, packet);
                av_packet_unref(packet);
                av_packet_free(&packet);
                packet = NULL;
                if (ret != 0)
                {
                    *desc->_errorCode = -21;
                    *desc->_looping = false;
                    break;
                }
                while (true)
                {
                    if (frame == NULL)
                    {
                        frame = av_frame_alloc();
                        if (frame == NULL)
                        {
                            *desc->_errorCode = -22;
                            *desc->_looping = false;
                            break;
                        }
                    }
                    ret = avcodec_receive_frame(desc->_audioCodecCtx, frame);
                    if (ret < 0)
                    {
                        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                        {
                            *desc->_errorCode = -23;
                            *desc->_looping = false;
                        }
                        break;
                    }
                    double duration = frame->nb_samples / (double)frame->sample_rate;
                    desc->_audioFrames->Enqueue(frame, duration);
                    frame = NULL;
                }
            }
            if (frame != NULL)
            {
                av_frame_unref(frame);
                av_frame_free(&frame);
                frame = NULL;
            }
            return 0;
        }

		struct VideoDesc
		{
			bool* _looping;
			State* _state;
			long long* _errorCode;
			double* _time;
			long long* _ts;
			PtrQueue* _videoPackets;
			PtrQueue* _videoFrames;
			AVStream* _videoStream;
			AVCodecContext* _videoCodecCtx;
		};

		static int VideoThread(void* param)
		{
			VideoDesc* desc = (VideoDesc*)param;
			AVRational* timebase = &desc->_videoStream->time_base;
			AVFrame* frame = NULL;
			while (*desc->_looping)
			{
				if (desc->_videoFrames->GetDuration() > 2.0)
				{
					SDL_Delay(100);
					continue;
				}
				AVPacket* packet = (AVPacket*)desc->_videoPackets->Dequeue();
				if (packet == NULL)
					continue;
				int ret = avcodec_send_packet(desc->_videoCodecCtx, packet);
				av_packet_unref(packet);
				av_packet_free(&packet);
				packet = NULL;
				if (ret != 0)
				{
					*desc->_errorCode = -21;
					*desc->_looping = false;
					break;
				}
				while (true)
				{
					if (frame == NULL)
					{
						frame = av_frame_alloc();
						if (frame == NULL)
						{
							*desc->_errorCode = -22;
							*desc->_looping = false;
							break;
						}
					}
					int ret = avcodec_receive_frame(desc->_videoCodecCtx, frame);
					if (ret < 0)
					{
						if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
						{
							*desc->_errorCode = -23;
							*desc->_looping = false;
						}
						break;
					}
					double duration = frame->pkt_duration * timebase->num / (double)timebase->den;
					desc->_videoFrames->Enqueue(frame, duration);
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
        
        static bool error(long long& error, long long code)
        {
            error = code;
            return true;
        }
        
        static int MainThread(void* param)
        {
            AVFormatContext* mudexFormatCtx = NULL;
            AVCodecContext* audioCodecCtx = NULL;
            AVStream* audioStream = NULL;
            AVCodecContext* videoCodecCtx = NULL;
            AVStream* videoStream = NULL;
            SDL_Thread* audioThread = NULL;
            SDL_Thread* videoThread = NULL;
            SDL_Window* videoWindow = NULL;
            SDL_Renderer* videoRenderer = NULL;
            SDL_Texture* videoTexture = NULL;
            PtrQueue audioPackets;
            PtrQueue videoPackets;
            PtrQueue audioFrames;
            PtrQueue videoFrames;
            AudioDesc audioDesc;
			VideoDesc videoDesc;
            
            SFPlayerImpl* impl = (SFPlayerImpl*)param;
            long long errorCode = 0;
            if (avformat_open_input(&mudexFormatCtx, impl->_filename.c_str(), NULL, NULL) != 0 && error(errorCode, -1))
                goto end;
            if (avformat_find_stream_info(mudexFormatCtx, NULL) < 0 && error(errorCode, -2))
                goto end;
            for (int i = 0; i < (int)mudexFormatCtx->nb_streams; i++)
            {
                if (impl->_flag != NoAudio && audioStream == NULL && mudexFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                    audioStream = mudexFormatCtx->streams[i];
                if (impl->_flag != NoVideo && videoStream == NULL && mudexFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                    videoStream = mudexFormatCtx->streams[i];
            }
            if (((audioStream == NULL && videoStream == NULL) || (audioStream == NULL && impl->_flag == NoVideo) ||
                (videoStream == NULL && impl->_flag == NoAudio)) && error(errorCode, -3))
                goto end;
            if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 && error(errorCode, -4))
                goto end;
            if (audioStream != NULL)
            {
                AVCodec* audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);
                if (audioCodec == NULL && error(errorCode, -5))
                    goto end;
                if ((audioCodecCtx = avcodec_alloc_context3(audioCodec)) == NULL && error(errorCode, -6))
                    goto end;
                if (avcodec_parameters_to_context(audioCodecCtx, audioStream->codecpar) != 0 && error(errorCode, -7))
                    goto end;
                if (avcodec_open2(audioCodecCtx, audioCodec, NULL) != 0 && error(errorCode, -8))
                    goto end;
            }
            if (videoStream != NULL)
            {
                AVCodec* videoCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
                if (videoCodec == NULL && error(errorCode, -9))
                    goto end;
                if ((videoCodecCtx = avcodec_alloc_context3(videoCodec)) == NULL && error(errorCode, -10))
                    goto end;
                if (avcodec_parameters_to_context(videoCodecCtx, videoStream->codecpar) != 0 && error(errorCode, -11))
                    goto end;
                if (avcodec_open2(videoCodecCtx, videoCodec, NULL) != 0 && error(errorCode, -12))
                    goto end;
            }
            if (audioStream != NULL)
            {
                audioDesc._looping = &impl->_looping;
				audioDesc._state = &impl->_state;
                audioDesc._errorCode = &impl->_errorCode;
                audioDesc._time = &impl->_time;
                audioDesc._ts = &impl->_ts;
                audioDesc._audioPackets = &audioPackets;
                audioDesc._audioFrames = &audioFrames;
                audioDesc._audioCodecCtx = audioCodecCtx;
                SDL_AudioSpec want;
                SDL_zero(want);
                want.freq = audioCodecCtx->sample_rate;
                want.format = AUDIO_S16SYS;
                want.channels = 2;
                want.samples = 1024;
                want.callback = AudioDevice;
                want.userdata = &audioDesc;
                SDL_AudioSpec real;
                SDL_zero(real);
                SDL_AudioDeviceID audioDeviceId = SDL_OpenAudioDevice(NULL, 0, &want, &real, 0);
                if (audioDeviceId == 0 && error(errorCode, -13))
                    goto end;
                SDL_PauseAudioDevice(audioDeviceId, 0);
                audioThread = SDL_CreateThread(AudioThread, "AudioThread", &audioDesc);
                if (audioThread == NULL && error(errorCode, -14))
                    goto end;
            }
            if (videoStream != NULL)
            {
				videoDesc._looping = &impl->_looping;
				videoDesc._state = &impl->_state;
				videoDesc._errorCode = &impl->_errorCode;
				videoDesc._time = &impl->_time;
				videoDesc._ts = &impl->_ts;
				videoDesc._videoPackets = &videoPackets;
				videoDesc._videoFrames = &videoFrames;
				videoDesc._videoStream = videoStream;
				videoDesc._videoCodecCtx = videoCodecCtx;

                int width = videoCodecCtx->width;
                int height = videoCodecCtx->height;
                videoWindow = SDL_CreateWindow("MainWindow", 100, 100, width/2, height/2, SDL_WINDOW_SHOWN);
                if (videoWindow == NULL && error(errorCode, -15))
                    goto end;
                videoRenderer = SDL_CreateRenderer(videoWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
                if (videoRenderer == NULL && error(errorCode, -16))
                    goto end;
                videoTexture = SDL_CreateTexture(videoRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
                if (videoTexture == NULL && error(errorCode, -17))
                    goto end;
                videoThread = SDL_CreateThread(VideoThread, "VideoThread", &videoDesc);
                if (videoThread == NULL && error(errorCode, -18))
                    goto end;
            }
			impl->_state = Buffering;
			impl->_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			long long startTs = impl->_ts;
			int ind = 0;

			static const char* StateNames[] = 
			{
				"Closed(0)",
				"Paused(1)",
				"Buffering(2)",
				"Playing(3)",
			};

            while (impl->_looping)
            {
				long long currTs = std::chrono::high_resolution_clock::now().time_since_epoch().count();
				double tt = (currTs - startTs) / 1000000000.0;
				log("Main[%d] %.3fs %s A[%d,%d] V[%d,%d]\n", ind++, tt, StateNames[impl->_state], audioPackets.Size(), audioFrames.Size(), videoPackets.Size(), videoFrames.Size());
                SDL_Event event;
                if (SDL_PollEvent(&event))
                {
					log("Main: SDL event process: %d\n", event.type);
                    switch (event.type)
                    {
                    case SDL_QUIT:
                        impl->_looping = false;
                        break;
                    };
					continue;
                }
				if (impl->_state == Buffering)
				{
					if ((audioThread && videoThread && audioFrames.GetDuration() > 2.0 && videoFrames.GetDuration() > 2.0) ||
						(audioThread && audioFrames.GetDuration() > 2.0) || (videoThread && videoFrames.GetDuration() > 2.0))
					{
						impl->_state = Playing;
						impl->_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
						log("Main: State %s -> %s \n", StateNames[Buffering], StateNames[Playing]);
						continue;
					}
				}
				if (impl->_state == Playing && videoThread)
				{
					AVFrame* frame = (AVFrame*)videoFrames.PeekFront();
					if (frame == NULL)
					{
						impl->_state = Buffering;
						impl->_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
						log("Main: State %s -> %s \n", StateNames[Playing], StateNames[Buffering]);
						continue;
					}
					else
					{
						long long pts = frame->best_effort_timestamp;
						double time = pts * videoStream->time_base.num / (double)videoStream->time_base.den;
						if (time <= impl->_time)
						{
							if (SDL_UpdateYUVTexture(videoTexture, NULL, frame->data[0], frame->linesize[0],
								frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]) != 0 && error(errorCode, -11))
								goto end;
							if (SDL_RenderCopy(videoRenderer, videoTexture, NULL, NULL) != 0 && error(errorCode, -13))
								goto end;
							SDL_RenderPresent(videoRenderer);
							frame = (AVFrame*)videoFrames.Dequeue();
							av_frame_unref(frame);
							av_frame_free(&frame);
							frame = NULL;
							log("Main: Render video frame pts:%.3f\n", time);
						}
						if (audioThread == NULL)
						{
							long long nanoTs = std::chrono::high_resolution_clock::now().time_since_epoch().count();
							long long nanoDiff = nanoTs - impl->_ts;
							double diffSecs = nanoDiff / (double)1000000000l;
							if (nanoDiff > 0)
							{
								impl->_time += diffSecs;
								impl->_ts = nanoTs;
							}
							log("Main: Advance time %.3fs:(+%.3fs)\n", impl->_time, diffSecs);
						}
						continue;
					}
				}
                if ((videoThread && videoPackets.GetDuration() < 2.0) || (audioThread && audioPackets.GetDuration() < 2.0))
                {//demux
                    AVPacket* packet = av_packet_alloc();
                    if (packet == NULL && error(errorCode, -23))
                        goto end;
                    int ret = av_read_frame(mudexFormatCtx, packet);
                    if (ret != 0)
                    {
                        if (ret == AVERROR_EOF)
                            break;
                        if (error(errorCode, -24))
                            goto end;
                    }
                    if (audioThread && packet->stream_index == audioStream->index)
                    {
                        double duration = packet->duration * audioStream->time_base.num / (double)audioStream->time_base.den;
                        audioPackets.Enqueue(packet, duration);
						log("Main: Demux audio packet A[%d+1, %d] V[%d,%d]\n", audioPackets.Size(), audioFrames.Size(), videoPackets.Size(), videoFrames.Size());
                    }
                    else if (videoThread && packet->stream_index == videoStream->index)
                    {
                        double duration = packet->duration * videoStream->time_base.num / (double)videoStream->time_base.den;
                        videoPackets.Enqueue(packet, duration);
						log("Main: Demux video packet A[%d, %d] V[%d+1,%d]\n", audioPackets.Size(), audioFrames.Size(), videoPackets.Size(), videoFrames.Size());
                    }
                    else
                    {
                        av_packet_unref(packet);
                        av_packet_free(&packet);
                        packet = NULL;
						log("Main: Demux unknown packet\n");
                    }
					continue;
                }
				else
				{
					log("Main: empty loop delay");
					SDL_Delay(100);
				}
            }
                            
            while (impl->_looping)
            {
                if ((audioThread && audioPackets.Size() == 0 && audioFrames.Size() == 0 &&
                     videoThread && videoPackets.Size() == 0 && videoFrames.Size() == 0) ||
                    (audioThread && audioPackets.Size() == 0 && audioFrames.Size() == 0) ||
                    (videoThread && videoPackets.Size() == 0 && videoFrames.Size() == 0))
                    break;
            }

        end:
            impl->_looping = false;
            if (videoThread != NULL)
            {
                SDL_WaitThread(videoThread, NULL);
                videoThread = NULL;
            }
            if (audioThread != NULL)
            {
                SDL_WaitThread(audioThread, NULL);
                audioThread = NULL;
            }
            for (AVPacket* packet = (AVPacket*)videoPackets.Dequeue(); packet != NULL; packet = (AVPacket*)videoPackets.Dequeue())
            {
                av_packet_unref(packet);
                av_packet_free(&packet);
            }
            for (AVPacket* packet = (AVPacket*)audioPackets.Dequeue(); packet != NULL; packet = (AVPacket*)audioPackets.Dequeue())
            {
                av_packet_unref(packet);
                av_packet_free(&packet);
            }
            
            for (AVFrame* frame = (AVFrame*)videoFrames.Dequeue(); frame != NULL; frame = (AVFrame*)videoFrames.Dequeue())
            {
                av_frame_unref(frame);
                av_frame_free(&frame);
            }
            for (AVFrame* frame = (AVFrame*)audioFrames.Dequeue(); frame != NULL; frame = (AVFrame*)audioFrames.Dequeue())
            {
                av_frame_unref(frame);
                av_frame_free(&frame);
            }
            if (audioCodecCtx != NULL)
            {
                avcodec_close(audioCodecCtx);
                audioCodecCtx = NULL;
            }
            if (videoCodecCtx != NULL)
            {
                avcodec_close(videoCodecCtx);
                videoCodecCtx = NULL;
            }
            audioStream = NULL;
            videoStream = NULL;
            if (mudexFormatCtx != NULL)
            {
                avformat_close_input(&mudexFormatCtx);
                mudexFormatCtx = NULL;
            }
            if (videoTexture != NULL)
            {
                SDL_DestroyTexture(videoTexture);
                videoTexture = NULL;
            }
            if (videoRenderer != NULL)
            {
                SDL_RenderClear(videoRenderer);
                videoRenderer = NULL;
            }
            if (videoWindow != NULL)
            {
                SDL_DestroyWindow(videoWindow);
                videoWindow = NULL;
            }
            SDL_Quit();
            return 0;
        }
        
        std::string _filename;
        Flag _flag;
        State _state;
        double _time;
        long long _ts;
        bool _looping;
        long long _errorCode;
        SDL_Thread* _mainthread;
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
        _impl->_filename = filename;
        _impl->_flag = flag;
        _impl->_state = Closed;
        _impl->_time = 0.0;
        _impl->_ts = 0;
        _impl->_looping = true;
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

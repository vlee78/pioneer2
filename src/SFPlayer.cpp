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
		std::string _filename;
		Flag _flag;
		State _state;
		double _time;
		long long _ts;
		bool _looping;
		long long _errorCode;
		SDL_Thread* _mainthread;

        struct Desc
        {
			SFPlayerImpl* _impl;
			AVFormatContext* _demuxFormatCtx;
			AVCodecContext* _audioCodecCtx;
			AVStream* _audioStream;
            PtrQueue _audioPackets;
            PtrQueue _audioFrames;
			AVCodecContext* _videoCodecCtx;
			AVStream* _videoStream;
			PtrQueue _videoPackets;
			PtrQueue _videoFrames;
			bool _eof;
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
            Desc* desc = (Desc*)userdata;
			memset(stream, 0, len);
			if (desc->_impl->_looping == false || desc->_impl->_state != Playing)
				return;
            short* buffer0 = ((short*)stream) + 0;
            short* buffer1 = ((short*)stream) + 1;
            int bufchs = 2;
            int bufoff = 0;
            int bufmax = len / sizeof(short);
            while(true)
            {
                AVFrame* frame = (AVFrame*)desc->_audioFrames.PeekFront();
                if (frame == NULL)
                    break;
                AVSampleFormat format = (AVSampleFormat)frame->format;
                int sampleRate = frame->sample_rate;
                int channels = frame->channels;
                if (sampleRate != desc->_audioCodecCtx->sample_rate)
                {
                    desc->_impl->_errorCode = -201;
                    desc->_impl->_looping = false;
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
                        desc->_impl->_errorCode = -202;
                        desc->_impl->_looping = false;
                        return;
                    }
                    frame->width = frameoff;
                    if (frameoff >= framemax)
                    {
                        frame = (AVFrame*)desc->_audioFrames.Dequeue();
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
                    desc->_impl->_errorCode = -203;
                    desc->_impl->_looping = false;
                    return;
                }
            }
            int samples = bufmax / bufchs;
            double shift = samples / (double)desc->_audioCodecCtx->sample_rate;
            desc->_impl->_time += shift;
            desc->_impl->_ts = 0;
            //printf("Audio[%d]: %lld + %lld, %fs\n", ind++, impl->_playSampleOffset, impl->_playSampleCount, impl->_playTimeGlobal);
        }

		static int RenderThread(void* param)
		{
			Desc* desc = (Desc*)param;
			while (desc->_impl->_looping)
			{
				if (desc->_impl->_state != Playing)
				{
					SDL_Delay(100);
					continue;
				}
				AVFrame* frame = (AVFrame*)desc->_videoFrames.PeekFront();
				if (frame == NULL)
				{
					desc->_impl->_state = Buffering;
					desc->_impl->_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
					//log("Main: State %s -> %s \n", StateNames[Playing], StateNames[Buffering]);
					continue;
				}
				else
				{
					long long pts = frame->best_effort_timestamp;
					double time = pts * desc->_videoStream->time_base.num / (double)desc->_videoStream->time_base.den;
					if (time <= desc._impl->_time)
					{
						if (SDL_UpdateYUVTexture(videoTexture, NULL, frame->data[0], frame->linesize[0],
							frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]) != 0 && error(errorCode, -11))
							goto end;
						if (SDL_RenderCopy(videoRenderer, videoTexture, NULL, NULL) != 0 && error(errorCode, -13))
							goto end;
						SDL_RenderPresent(videoRenderer);
						frame = (AVFrame*)desc._videoFrames.Dequeue();
						av_frame_unref(frame);
						av_frame_free(&frame);
						frame = NULL;
						log("Main: Render video frame pts:%.3f time:0.3fs ts=%lld\n", time, desc._impl->_time, desc._impl->_ts / 1000000);
						if (audioThread == NULL)
						{
							long long nanoTs = std::chrono::high_resolution_clock::now().time_since_epoch().count();
							long long nanoDiff = nanoTs - desc._impl->_ts;
							double diffSecs = nanoDiff / (double)1000000000l;
							if (nanoDiff > 0)
							{
								desc._impl->_time += diffSecs;
								desc._impl->_ts = nanoTs;
							}
							log("Main: Advance video time %.3fs:(+%.3fs) ts=%lld\n", desc._impl->_time, diffSecs, desc._impl->_ts / 1000000);
						}
						continue;
					}
					if (audioThread == NULL)
					{
						long long nanoTs = std::chrono::high_resolution_clock::now().time_since_epoch().count();
						long long nanoDiff = nanoTs - desc._impl->_ts;
						double diffSecs = nanoDiff / (double)1000000000l;
						if (nanoDiff > 0)
						{
							desc._impl->_time += diffSecs;
							desc._impl->_ts = nanoTs;
						}
						log("Main: Advance video time %.3fs:(+%.3fs) ts=%lld\n", desc._impl->_time, diffSecs, desc._impl->_ts / 1000000);
					}
				}
			}
			return 0;
		}
        
        static int AudioThread(void* param)
        {
            Desc* desc = (Desc*)param;
            AVFrame* frame = NULL;
            while (desc->_impl->_looping)
            {
				if (desc->_audioFrames.GetDuration() > 2.0)
				{
					SDL_Delay(100);
					continue;
				}
                AVPacket* packet = (AVPacket*)desc->_audioPackets.Dequeue();
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
                    desc->_impl->_errorCode = -21;
                    desc->_impl->_looping = false;
                    break;
                }
                while (true)
                {
                    if (frame == NULL)
                    {
                        frame = av_frame_alloc();
                        if (frame == NULL)
                        {
                            desc->_impl->_errorCode = -22;
                            desc->_impl->_looping = false;
                            break;
                        }
                    }
                    ret = avcodec_receive_frame(desc->_audioCodecCtx, frame);
                    if (ret < 0)
                    {
                        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                        {
                            desc->_impl->_errorCode = -23;
                            desc->_impl->_looping = false;
                        }
                        break;
                    }
                    double duration = frame->nb_samples / (double)frame->sample_rate;
                    desc->_audioFrames.Enqueue(frame, duration);
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

		static int VideoThread(void* param)
		{
			Desc* desc = (Desc*)param;
			AVRational* timebase = &desc->_videoStream->time_base;
			AVFrame* frame = NULL;
			int ind = 0;
			while (desc->_impl->_looping)
			{
				log("  Video[%d] A[%d,%d] V[%d,%d] time:%0.3fs ts=%lld\n", ind++, desc->_audioPackets.Size(), desc->_audioFrames.Size(), desc->_videoPackets.Size(), desc->_videoFrames.Size(), desc->_impl->_time, desc->_impl->_ts/1000000);
				if (desc->_videoFrames.GetDuration() > 2.0)
				{
					log("  Video: frames queue full, delay 100ms\n");
					SDL_Delay(100);
					continue;
				}
				AVPacket* packet = (AVPacket*)desc->_videoPackets.Dequeue();
				if (packet == NULL)
				{
					log("  Video: packet queue empty, delay 100ms\n");
					SDL_Delay(100);
					continue;
				}
				int ret = avcodec_send_packet(desc->_videoCodecCtx, packet);
				av_packet_unref(packet);
				av_packet_free(&packet);
				packet = NULL;
				if (ret != 0)
				{
					log("  Video: decode send packet failed, terminating\n");
					desc->_impl->_errorCode = -21;
					desc->_impl->_looping = false;
					break;
				}
				while (true)
				{
					if (frame == NULL)
					{
						frame = av_frame_alloc();
						if (frame == NULL)
						{
							log("  Video: alloc frame failed, terminating\n");
							desc->_impl->_errorCode = -22;
							desc->_impl->_looping = false;
							break;
						}
					}
					int ret = avcodec_receive_frame(desc->_videoCodecCtx, frame);
					if (ret < 0)
					{
						if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
						{
							log("  Video: decode recv frame failed, terminating\n");
							desc->_impl->_errorCode = -23;
							desc->_impl->_looping = false;
						}
						else
							log("  Video: descode patial frame, continue\n");
						break;
					}
					double duration = frame->pkt_duration * timebase->num / (double)timebase->den;
					desc->_videoFrames.Enqueue(frame, duration);
					frame = NULL;
					log("  Video: descode one frame A[%d,%d] V[%d,++%d]\n", desc->_audioPackets.Size(), desc->_audioFrames.Size(), desc->_videoPackets.Size(), desc->_videoFrames.Size());
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
        
		static int DemuxThread(void* param)
		{
			Desc* desc = (Desc*)param;
			while (desc->_impl->_looping)
			{
				if (desc->_eof || (desc->_impl->_state != Buffering && desc->_impl->_state != Playing))
				{
					SDL_Delay(100);
					continue;
				}
				if ((desc->_videoStream && desc->_videoPackets.GetDuration() >= 2.5) || (desc->_audioStream && desc->_audioPackets.GetDuration() >= 2.5))
				{
					SDL_Delay(100);
					continue;
				}
				AVPacket* packet = av_packet_alloc();
				if (packet == NULL)
				{
					desc->_impl->_errorCode = -1;
					desc->_impl->_looping = false;
					break;
				}
				int ret = av_read_frame(desc->_demuxFormatCtx, packet);
				if (ret == 0)
				{
					if (desc->_audioStream && packet->stream_index == desc->_audioStream->index)
					{
						double duration = packet->duration * desc->_audioStream->time_base.num / (double)desc->_audioStream->time_base.den;
						desc->_audioPackets.Enqueue(packet, duration);
					}
					else if (desc->_videoStream && packet->stream_index == desc->_videoStream->index)
					{
						double duration = packet->duration * desc->_videoStream->time_base.num / (double)desc->_videoStream->time_base.den;
						desc->_videoPackets.Enqueue(packet, duration);
					}
					packet = NULL;
				}
				else if (ret == AVERROR_EOF)
				{
					desc->_eof = true;
				}
				else 
				{
					desc->_impl->_errorCode = -2;
					desc->_impl->_looping = false;
					break;
				}
				if (packet != NULL)
				{
					av_packet_unref(packet);
					av_packet_free(&packet);
					packet = NULL;
				}
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
			Desc desc;
			desc._impl = (SFPlayerImpl*)param;
			desc._demuxFormatCtx = NULL;
			desc._audioCodecCtx = NULL;
			desc._audioStream = NULL;
			desc._videoCodecCtx = NULL;
			desc._videoStream = NULL;
			desc._eof = false;
            SDL_Thread* audioThread = NULL;
            SDL_Thread* videoThread = NULL;
			SDL_Thread* demuxThread = NULL;
			SDL_Thread* renderThread = NULL;

            SDL_Window* videoWindow = NULL;
            SDL_Renderer* videoRenderer = NULL;
            SDL_Texture* videoTexture = NULL;
            long long errorCode = 0;
            if (avformat_open_input(&desc._demuxFormatCtx, desc._impl->_filename.c_str(), NULL, NULL) != 0 && error(errorCode, -1))
                goto end;
            if (avformat_find_stream_info(desc._demuxFormatCtx, NULL) < 0 && error(errorCode, -2))
                goto end;
            for (int i = 0; i < (int)desc._demuxFormatCtx->nb_streams; i++)
            {
                if (desc._impl->_flag != NoAudio && desc._audioStream == NULL && desc._demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                    desc._audioStream = desc._demuxFormatCtx->streams[i];
                if (desc._impl->_flag != NoVideo && desc._videoStream == NULL && desc._demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                    desc._videoStream = desc._demuxFormatCtx->streams[i];
            }
            if (((desc._audioStream == NULL && desc._videoStream == NULL) || (desc._audioStream == NULL && desc._impl->_flag == NoVideo) ||
                (desc._videoStream == NULL && desc._impl->_flag == NoAudio)) && error(errorCode, -3))
                goto end;
            if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 && error(errorCode, -4))
                goto end;
            if (desc._audioStream != NULL)
            {
                AVCodec* audioCodec = avcodec_find_decoder(desc._audioStream->codecpar->codec_id);
                if (audioCodec == NULL && error(errorCode, -5))
                    goto end;
                if ((desc._audioCodecCtx = avcodec_alloc_context3(audioCodec)) == NULL && error(errorCode, -6))
                    goto end;
                if (avcodec_parameters_to_context(desc._audioCodecCtx, desc._audioStream->codecpar) != 0 && error(errorCode, -7))
                    goto end;
                if (avcodec_open2(desc._audioCodecCtx, audioCodec, NULL) != 0 && error(errorCode, -8))
                    goto end;
            }
            if (desc._videoStream != NULL)
            {
                AVCodec* videoCodec = avcodec_find_decoder(desc._videoStream->codecpar->codec_id);
                if (videoCodec == NULL && error(errorCode, -9))
                    goto end;
                if ((desc._videoCodecCtx = avcodec_alloc_context3(videoCodec)) == NULL && error(errorCode, -10))
                    goto end;
                if (avcodec_parameters_to_context(desc._videoCodecCtx, desc._videoStream->codecpar) != 0 && error(errorCode, -11))
                    goto end;
                if (avcodec_open2(desc._videoCodecCtx, videoCodec, NULL) != 0 && error(errorCode, -12))
                    goto end;
            }
            if (desc._audioStream != NULL)
            {
                SDL_AudioSpec want;
                SDL_zero(want);
                want.freq = desc._audioCodecCtx->sample_rate;
                want.format = AUDIO_S16SYS;
                want.channels = 2;
                want.samples = 1024;
                want.callback = AudioDevice;
                want.userdata = &desc;
                SDL_AudioSpec real;
                SDL_zero(real);
                SDL_AudioDeviceID audioDeviceId = SDL_OpenAudioDevice(NULL, 0, &want, &real, 0);
                if (audioDeviceId == 0 && error(errorCode, -13))
                    goto end;
                SDL_PauseAudioDevice(audioDeviceId, 0);
                audioThread = SDL_CreateThread(AudioThread, "AudioThread", &desc);
                if (audioThread == NULL && error(errorCode, -14))
                    goto end;
            }
            if (desc._videoStream != NULL)
            {
                int width = desc._videoCodecCtx->width;
                int height = desc._videoCodecCtx->height;
                videoWindow = SDL_CreateWindow("MainWindow", 100, 100, width/2, height/2, SDL_WINDOW_SHOWN);
                if (videoWindow == NULL && error(errorCode, -15))
                    goto end;
                videoRenderer = SDL_CreateRenderer(videoWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
                if (videoRenderer == NULL && error(errorCode, -16))
                    goto end;
                videoTexture = SDL_CreateTexture(videoRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
                if (videoTexture == NULL && error(errorCode, -17))
                    goto end;
                videoThread = SDL_CreateThread(VideoThread, "VideoThread", &desc);
                if (videoThread == NULL && error(errorCode, -18))
                    goto end;
            }
			desc._impl->_state = Buffering;
			desc._impl->_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			long long startTs = desc._impl->_ts;
			int ind = 0;

			static const char* StateNames[] = 
			{
				"Closed(0)",
				"Paused(1)",
				"Buffering(2)",
				"Playing(3)",
			};

			demuxThread = SDL_CreateThread(DemuxThread, "DemuxThread", &desc);
			if (demuxThread == NULL && error(errorCode, -19))
				goto end;
			renderThread = SDL_CreateThread(RenderThread, "RenderThread", &desc);
			if (renderThread == NULL)
				goto end;

            while (desc._impl->_looping)
            {
				long long currTs = std::chrono::high_resolution_clock::now().time_since_epoch().count();
				double tt = (currTs - startTs) / 1000000000.0;
				log("Main[%d] %.3fs %s A[%d,%d] V[%d,%d] time:%0.3f ts=%lld\n", ind++, tt, StateNames[desc._impl->_state], desc._audioPackets.Size(), desc._audioFrames.Size(), desc._videoPackets.Size(), desc._videoFrames.Size(), desc._impl->_time, desc._impl->_ts/1000000);
                SDL_Event event;
                if (SDL_PollEvent(&event))
                {
					log("Main: SDL event process: %d\n", event.type);
                    switch (event.type)
                    {
                    case SDL_QUIT:
						desc._impl->_looping = false;
                        break;
                    };
					continue;
                }
				if (desc._impl->_state == Buffering)
				{
					if ((audioThread && videoThread && desc._audioFrames.GetDuration() > 2.0 && desc._videoFrames.GetDuration() > 2.0) ||
						(audioThread && desc._audioFrames.GetDuration() > 2.0) || (videoThread && desc._videoFrames.GetDuration() > 2.0))
					{
						desc._impl->_state = Playing;
						desc._impl->_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
						log("Main: State %s -> %s \n", StateNames[Buffering], StateNames[Playing]);
						continue;
					}
				}
            }
                            
            while (desc._impl->_looping)
            {
                if ((audioThread && desc._audioPackets.Size() == 0 && desc._audioFrames.Size() == 0 &&
                     videoThread && desc._videoPackets.Size() == 0 && desc._videoFrames.Size() == 0) ||
                    (audioThread && desc._audioPackets.Size() == 0 && desc._audioFrames.Size() == 0) ||
                    (videoThread && desc._videoPackets.Size() == 0 && desc._videoFrames.Size() == 0))
                    break;
            }

        end:
			desc._impl->_looping = false;
			if (renderThread != NULL)
			{
				SDL_WaitThread(renderThread, NULL);
				renderThread = NULL;
			}
			if (demuxThread != NULL)
			{
				SDL_WaitThread(demuxThread, NULL);
				demuxThread = NULL;
			}
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
            for (AVPacket* packet = (AVPacket*)desc._videoPackets.Dequeue(); packet != NULL; packet = (AVPacket*)desc._videoPackets.Dequeue())
            {
                av_packet_unref(packet);
                av_packet_free(&packet);
            }
            for (AVPacket* packet = (AVPacket*)desc._audioPackets.Dequeue(); packet != NULL; packet = (AVPacket*)desc._audioPackets.Dequeue())
            {
                av_packet_unref(packet);
                av_packet_free(&packet);
            }
            
            for (AVFrame* frame = (AVFrame*)desc._videoFrames.Dequeue(); frame != NULL; frame = (AVFrame*)desc._videoFrames.Dequeue())
            {
                av_frame_unref(frame);
                av_frame_free(&frame);
            }
            for (AVFrame* frame = (AVFrame*)desc._audioFrames.Dequeue(); frame != NULL; frame = (AVFrame*)desc._audioFrames.Dequeue())
            {
                av_frame_unref(frame);
                av_frame_free(&frame);
            }
            if (desc._audioCodecCtx != NULL)
            {
                avcodec_close(desc._audioCodecCtx);
				desc._audioCodecCtx = NULL;
            }
            if (desc._videoCodecCtx != NULL)
            {
                avcodec_close(desc._videoCodecCtx);
				desc._videoCodecCtx = NULL;
            }
			desc._audioStream = NULL;
			desc._videoStream = NULL;
            if (desc._demuxFormatCtx != NULL)
            {
                avformat_close_input(&desc._demuxFormatCtx);
				desc._demuxFormatCtx = NULL;
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

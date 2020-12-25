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

#define DEMUX_SATURATE 5//A和V的packet队列尾同时大于time+DEMUX_SATURATE的话，暂停demux
#define AUDIO_SATURATE 5//A的frame队列尾大于time+AUDIO_SATURATE的话，暂停audio解码
#define VIDEO_SATURATE 5//V的frame队列尾大于time+VIDEO_SATURATE的话，暂停video解码
#define RENDER_BUFFERED 4//A和V的frame队列尾大于time+RENDER_BUFFERED的话，从buffering开始playing

namespace pioneer
{
    static const char* StateNames[] =
    {
        "Closed(0)",
        "Paused(1)",
        "Buffering(2)",
        "Playing(3)",
    };

	class PacketQueue
	{
	public:
		SDL_mutex* _mutex;
		std::list<AVPacket*> _queue;

		PacketQueue()
		{
			_mutex = SDL_CreateMutex();
		}

		~PacketQueue()
		{
			Clear();
			SDL_DestroyMutex(_mutex);
		}

		void Clear()
		{
			SDL_LockMutex(_mutex);
			for (auto it = _queue.begin(); it != _queue.end(); it++)
			{
				AVPacket* packet= *it;
				av_packet_unref(packet);
				av_packet_free(&packet);
			}
			_queue.clear();
			SDL_UnlockMutex(_mutex);
		}

		int Size()
		{
			SDL_LockMutex(_mutex);
			int size = (int)_queue.size();
			SDL_UnlockMutex(_mutex);
			return size;
		}

		AVPacket* Dequeue()
		{
			SDL_LockMutex(_mutex);
			AVPacket* packet = NULL;
			if (_queue.size() > 0)
			{
				packet = _queue.front();
				_queue.pop_front();
			}
			SDL_UnlockMutex(_mutex);
			return packet;
		}

		void Enqueue(AVPacket* packet)
		{
			SDL_LockMutex(_mutex);
			_queue.push_back(packet);
			SDL_UnlockMutex(_mutex);
		}

		long long Head()
		{
			SDL_LockMutex(_mutex);
			long long head = 0;
			if (_queue.size() > 0)
				head = _queue.front()->pts;
			SDL_UnlockMutex(_mutex);
			return head;
		}

		double Tail()
		{
			SDL_LockMutex(_mutex);
			double tail = 0;
			if (_queue.size() > 0)
			{
				AVPacket* packet = _queue.back();
				tail = packet->pts + packet->duration;
			}
			SDL_UnlockMutex(_mutex);
			return tail;
		}
	};

	class FrameQueue
	{
	public:
		SDL_mutex* _mutex;
		std::list<AVFrame*> _queue;

		FrameQueue()
		{
			_mutex = SDL_CreateMutex();
		}

		~FrameQueue()
		{
			Clear();
			SDL_DestroyMutex(_mutex);
		}

		void Clear()
		{
			SDL_LockMutex(_mutex);
			for (auto it = _queue.begin(); it != _queue.end(); it++)
			{
				AVFrame* frame = *it;
				av_frame_unref(frame);
				av_frame_free(&frame);
			}
			_queue.clear();
			SDL_UnlockMutex(_mutex);
		}

		int Size()
		{
			SDL_LockMutex(_mutex);
			int size = (int)_queue.size();
			SDL_UnlockMutex(_mutex);
			return size;
		}

		AVFrame* PeekFront()
		{
			SDL_LockMutex(_mutex);
			AVFrame* frame = NULL;
			if (_queue.size() > 0)
				frame = _queue.front();
			SDL_UnlockMutex(_mutex);
			return frame;
		}

		void PopFront()
		{
			SDL_LockMutex(_mutex);
			if (_queue.size() > 0)
			{
				AVFrame* frame = _queue.front();
				av_frame_unref(frame);
				av_frame_free(&frame);
				_queue.pop_front();
			}
			SDL_UnlockMutex(_mutex);
		}

		AVFrame* Dequeue()
		{
			SDL_LockMutex(_mutex);
			AVFrame* frame = NULL;
			if (_queue.size() > 0)
			{
				frame = _queue.front();
				_queue.pop_front();
			}
			SDL_UnlockMutex(_mutex);
			return frame;
		}

		void Enqueue(AVFrame* frame)
		{
			SDL_LockMutex(_mutex);
			_queue.push_back(frame);
			SDL_UnlockMutex(_mutex);
		}

		long long Head()
		{
			SDL_LockMutex(_mutex);
			long long head = 0;
			if (_queue.size() > 0)
			{
				AVFrame* frame = _queue.front();
				head = frame->pts + frame->height;
			}
			SDL_UnlockMutex(_mutex);
			return head;
		}

		double Tail()
		{
			SDL_LockMutex(_mutex);
			double tail = 0;
			if (_queue.size() > 0)
			{
				AVFrame* frame = _queue.back();
				tail = frame->pts + frame->pkt_duration;
			}
			SDL_UnlockMutex(_mutex);
			return tail;
		}
	};

    class SFPlayer::SFPlayerImpl
    {
    public:
		std::string _filename;
		Flag _flag;
		State _state;
		long long _time;
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
			AVRational _audioTimebase;
            PacketQueue _audioPackets;
            FrameQueue _audioFrames;
			AVCodecContext* _videoCodecCtx;
			AVStream* _videoStream;
			AVRational _videoTimebase;
			PacketQueue _videoPackets;
			FrameQueue _videoFrames;

			AVRational _timebase;

			bool _eof;
            SDL_Thread* _demuxThread;
            SDL_Thread* _audioThread;
            SDL_Thread* _videoThread;
			SDL_Thread* _renderThread;
			SDL_Window* _renderWindow;
			SDL_Renderer* _renderRenderer;
			SDL_Texture* _renderTexture;

			void log(const char* format, ...)
			{
				static FILE* file = NULL;
				if (file == NULL)
					file = fopen("log.txt", "wb");
				static long long lastTs = std::chrono::high_resolution_clock::now().time_since_epoch().count();
				double time = (std::chrono::high_resolution_clock::now().time_since_epoch().count() - lastTs) / (double)1000000000;
				fprintf(file, "(%.3f, %.3f) A:[%.3f, %.3f] V:[%.3f, %.3f] a:[%.3f, %.3f] v:[%.3f, %.3f] \n", time, _impl->_time, 
					_audioFrames.Head(), _audioFrames.Tail(), _videoFrames.Head(), _videoFrames.Tail(),
					_audioPackets.Head(), _audioPackets.Tail(), _videoPackets.Head(), _videoPackets.Tail());
				printf("(%.3f, %.3f) A:[%.3f, %.3f] V:[%.3f, %.3f] a:[%.3f, %.3f] v:[%.3f, %.3f] \n", time, _impl->_time,
					_audioFrames.Head(), _audioFrames.Tail(), _videoFrames.Head(), _videoFrames.Tail(),
					_audioPackets.Head(), _audioPackets.Tail(), _videoPackets.Head(), _videoPackets.Tail());
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

			void swstate(State to)
			{
				log("swstate: %d(%s) -> %d (%s)\n", _impl->_state, StateNames[_impl->_state], to, StateNames[to]);
				_impl->_state = to;
				_impl->_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			}
        };

        static void AudioDevice(void* userdata, Uint8* stream, int len)
        {
            Desc* desc = (Desc*)userdata;
			memset(stream, 0, len);
			if (desc->_impl->_looping == false || desc->_impl->_state != Playing)
				return;
			int sampleRate = desc->_audioCodecCtx->sample_rate;
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
				double time = frame->pts * desc->_audioTimebase;
                int channels = frame->channels;
                if (sampleRate != frame->sample_rate)
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
                    frame->width = frameoff;//音频帧内的已经读取输出的偏移,单位为frame
					frame->height = (int)((long long)frameoff * sampleRate / desc->_audioStream->time_base.num / desc->_audioStream->time_base.den);//将帧内偏移转换为timebase
					if (frameoff >= framemax)
						desc->_audioFrames.PopFront();
                    if (bufoff >= bufmax)
                        break;
                }
                else
                {
                    desc->_impl->_errorCode = -203;
                    desc->_impl->_looping = false;
                    return;
                }
            }
            long long samples = bufoff / bufchs;//总共消耗了frame queue多少sample
			long long shift = (long long)samples * sampleRate / desc->_audioStream->time_base.num / desc->_audioStream->time_base.den;
            desc->_impl->_time += shift;
            desc->_impl->_ts = 0;
        }
		
		static int RenderThread(void* param)
		{
			Desc* desc = (Desc*)param;
			while (desc->_impl->_looping)
			{
				if (desc->_impl->_state != Playing || desc->_videoStream == NULL)
				{
					SDL_Delay(100);
					continue;
				}
				AVFrame* frame = (AVFrame*)desc->_videoFrames.PeekFront();
				if (frame != NULL)
				{
					long long pts = frame->best_effort_timestamp;
					double time = pts * desc->_videoTimebase;
					if (time <= desc->_impl->_time)
					{
						if (SDL_UpdateYUVTexture(desc->_renderTexture, NULL, frame->data[0], frame->linesize[0], frame->data[1],
							frame->linesize[1], frame->data[2], frame->linesize[2]) != 0)
						{
							error(desc, -401);
							break;
						}
						if (SDL_RenderCopy(desc->_renderRenderer, desc->_renderTexture, NULL, NULL) != 0)
						{
							error(desc, -402);
							break;
						}
						SDL_RenderPresent(desc->_renderRenderer);
						frame = (AVFrame*)desc->_videoFrames.Dequeue();
						av_frame_unref(frame);
						av_frame_free(&frame);
						frame = NULL;
					}
				}
				if (desc->_audioStream == NULL)
				{
					long long nanoTs = std::chrono::high_resolution_clock::now().time_since_epoch().count();
					long long nanoDiff = nanoTs - desc->_impl->_ts;
					double diffSecs = nanoDiff / (double)1000000000l;
					if (nanoDiff > 0)
					{
						desc->_impl->_time += diffSecs;
						desc->_impl->_ts = nanoTs;
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
                AVPacket* packet = NULL;
				double endtime = desc->_impl->_time + AUDIO_SATURATE;
                if ((desc->_impl->_state != Buffering && desc->_impl->_state != Playing) ||
                    (desc->_audioFrames.Tail() >= endtime ) ||
                    (packet = (AVPacket*)desc->_audioPackets.Dequeue()) == NULL)
                {
                    SDL_Delay(0);
                    continue;
                }
                int ret = avcodec_send_packet(desc->_audioCodecCtx, packet);
                av_packet_unref(packet);
                av_packet_free(&packet);
                packet = NULL;
                if (ret != 0)
                {
                    error(desc, -301);
                    break;
                }
                while (desc->_impl->_looping)
                {
                    if (frame == NULL && (frame = av_frame_alloc()) == NULL)
                    {
                        error(desc, -302);
                        break;
                    }
                    ret = avcodec_receive_frame(desc->_audioCodecCtx, frame);
                    if (ret < 0)
                    {
                        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                            error(desc, -303);
                        break;
                    }
					double time = frame->pts * desc->_audioTimebase;
					if (time >= desc->_impl->_time)
					{//解压包时间戳小于当前指示时间，
						frame->width = 0;
						frame->height = 0;
						desc->_audioFrames.Enqueue(frame);
					}
					else
					{//丢掉
						av_frame_unref(frame);
						av_frame_free(&frame);
					}
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
            AVFrame* frame = NULL;
			while (desc->_impl->_looping)
			{
                AVPacket* packet = NULL;
				double endtime = desc->_impl->_time + VIDEO_SATURATE;
				if ((desc->_impl->_state != Buffering && desc->_impl->_state != Playing) ||
                    (desc->_videoFrames.Tail() >= endtime ) ||
                    (packet = (AVPacket*)desc->_videoPackets.Dequeue()) == NULL)
				{
					SDL_Delay(0);
					continue;
				}
				int ret = avcodec_send_packet(desc->_videoCodecCtx, packet);
				av_packet_unref(packet);
				av_packet_free(&packet);
				packet = NULL;
				if (ret != 0)
				{
                    error(desc, -201);
                    break;
				}
				while (desc->_impl->_looping)
				{
					if (frame == NULL && (frame = av_frame_alloc()) == NULL)
					{
                        error(desc, -202);
                        break;
					}
					int ret = avcodec_receive_frame(desc->_videoCodecCtx, frame);
					if (ret < 0)
                    {
                        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                            error(desc, -203);
                        break;
                    }
					double time = frame->pts * desc->_videoTimebase;
					if (time >= desc->_impl->_time)
					{
						double duration = frame->pkt_duration * desc->_videoTimebase;
						desc->_videoFrames.Enqueue(frame);
					}
					else
					{
						av_frame_unref(frame);
						av_frame_free(&frame);
					}
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

		static std::string PacketFlagDesc(int flag)
		{
			std::string res;
			if (flag & AV_PKT_FLAG_KEY)
				res += "KEY|";
			if (flag & AV_PKT_FLAG_CORRUPT)
				res += "CORRUPT|";
			if (flag & AV_PKT_FLAG_DISCARD)
				res += "DISCARD|";
			if (flag & AV_PKT_FLAG_TRUSTED)
				res += "TRUSTED|";
			if (flag & AV_PKT_FLAG_DISPOSABLE)
				res += "DISPOSABLE";
			return res;
		}

		static long long SecondsToSamples(double seconds, int sampleRate)
		{
			return (long long)(seconds * sampleRate);
		}
        
		static long long SecondsToTimebase(double seconds, AVStream* stream)
		{
			return (long long)(seconds * stream->time_base.den / stream->time_base.num);
		}

		static int LCM(int a, int b)
		{	
			int na = a;
			int nb = b;
			if (nb) while ((na %= nb) && (nb %= na));
			int gcd = na + nb;
			return a * b / gcd;
		}

		static AVRational CommonTimebase(AVRational* audioTimebase, AVRational* videoTimebase)
		{
			AVRational res;
			if (audioTimebase != NULL && videoTimebase != NULL)
				res = { LCM(audioTimebase->num, videoTimebase->num), LCM(audioTimebase->den, videoTimebase->den) };
			else if (audioTimebase != NULL)
				res = { audioTimebase->num, audioTimebase->den };
			else if (videoTimebase != NULL)
				res = { videoTimebase->num, videoTimebase->den };
			return res;
		}

		static int DemuxThread(void* param)
		{
			Desc* desc = (Desc*)param;
			while (desc->_impl->_looping)
			{
				if (desc->_eof || (desc->_impl->_state != Buffering && desc->_impl->_state != Playing))
				{
					SDL_Delay(0);
					continue;
				}
				//desc->_audioCodecCtx->sample_rate * DEMUX_SATURATE / (desc->_audioStream->time_base.num / desc->_audioStream->time_base.den);
				long long endtime = desc->_impl->_time + SecondsToTimebase(DEMUX_SATURATE, desc->_audioStream);
				if ((desc->_videoStream != NULL && desc->_audioStream != NULL && desc->_videoPackets.Tail() >= endtime && desc->_audioPackets.Tail() > endtime) ||
					(desc->_audioStream != NULL && desc->_audioPackets.Tail() >= endtime) ||
					(desc->_videoStream != NULL && desc->_videoPackets.Tail() >= endtime))
				{
					SDL_Delay(0);
					continue;
				}
				AVPacket* packet = av_packet_alloc();
                if (packet == NULL)
                {
                    error(desc, -101);
                    break;
                }
                int ret = av_read_frame(desc->_demuxFormatCtx, packet);
				if (ret == 0)
				{
					if (desc->_audioStream && packet->stream_index == desc->_audioStream->index)
					{
						double time = packet->pts * desc->_audioTimebase;
						double duration = packet->duration * desc->_audioStream->time_base.num / (double)desc->_audioStream->time_base.den;
						desc->_audioPackets.Enqueue(packet, time, duration);
						desc->log("\t\tDemux: Audio: time = %.3f (%.3f) %.3f, pts:%lld\n", time, duration, time + duration, packet->pts);
						packet = NULL;
					}
					else if (desc->_videoStream && packet->stream_index == desc->_videoStream->index)
					{
						double time = packet->pts * desc->_videoTimebase;
						double duration = packet->duration * desc->_videoStream->time_base.num / (double)desc->_videoStream->time_base.den;
						desc->_videoPackets.Enqueue(packet, time, duration);
						desc->log("\tDemux: Video: time = %.3f (%.3f) %.3f, pts:%lld %s\n", time, duration, time + duration, packet->pts, PacketFlagDesc(packet->flags).c_str());
						packet = NULL;
					}
				}
				else if (ret == AVERROR_EOF)
				{
                    desc->_eof = true;
				}
				else 
				{
                    error(desc, -102);
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

        static bool error(Desc* desc, long long code)
        {
            desc->_impl->_errorCode = code;
            desc->_impl->_looping = false;
            return true;
        }

		static long long Seek(Desc* desc, double seekto)
		{
			long long errorCode = 0;
			if (desc->_videoStream != NULL)
			{
				long long timestamp = seekto / desc->_videoTimebase;
				long long video_key_pts = -1;
				long long video_min_pts = -1;
				long long lastseek = -1;
				long long seekts = timestamp;
				AVPacket* packet = NULL;
				while (true)
				{
					if (seekts >= 0)
					{
						video_key_pts = -1;
						video_min_pts = -1;
						if (lastseek >= 0 && seekts >= lastseek)
						{
							errorCode = -1;
							break;
						}
						if (av_seek_frame(desc->_demuxFormatCtx, desc->_videoStream->index, seekts, AVSEEK_FLAG_BACKWARD) != 0)
						{
							errorCode = -2;
							break;
						}
						lastseek = seekts;
						seekts = -1;
					}
					packet = av_packet_alloc();
					if (packet == NULL)
					{
						errorCode = -3;
						break;
					}
					bool hit = false;
					int ret = av_read_frame(desc->_demuxFormatCtx, packet);
					if (ret == 0)
					{
						if (desc->_audioStream && packet->stream_index == desc->_audioStream->index)
						{
						}
						else if (desc->_videoStream && packet->stream_index == desc->_videoStream->index)
						{
							if (video_key_pts >= 0 && packet->flags & AV_PKT_FLAG_KEY)
								hit = true;
							if (video_key_pts == -1 && packet->flags & AV_PKT_FLAG_KEY)
							{
								video_key_pts = packet->pts;
								if (video_key_pts <= timestamp)
									hit = true;
							}
							if (video_min_pts == -1 || packet->pts < video_min_pts)
								video_min_pts = packet->pts;
						}
					}
					else if (ret == AVERROR_EOF)
					{
						if (video_key_pts == -1)
						{
							errorCode = -4;
							break;
						}
						hit = true;
					}
					else
					{
						errorCode = -4;
						break;
					}
					av_packet_unref(packet);
					av_packet_free(&packet);
					packet = NULL;
					if (hit)
					{
						if (video_key_pts < 0)
						{
							errorCode = -5;
							break;
						}
						if (video_key_pts <= timestamp)
							break;
						seekts = video_min_pts - 1;
					}
				}
				if (packet != NULL)
				{
					av_packet_unref(packet);
					av_packet_free(&packet);
					packet = NULL;
				}
				if (av_seek_frame(desc->_demuxFormatCtx, desc->_videoStream->index, lastseek, AVSEEK_FLAG_BACKWARD) != 0)
					errorCode = -6;
			}
			else if (desc->_audioStream)
			{
			}
			if (errorCode == 0)
				desc->_impl->_time = seekto;
			return errorCode;
		}
        
        static int MainThread(void* param)
        {
			Desc desc;
			desc._impl = (SFPlayerImpl*)param;
			desc._demuxFormatCtx = NULL;
			desc._audioCodecCtx = NULL;
			desc._audioStream = NULL;
			desc._audioTimebase = 0.0;
			desc._videoCodecCtx = NULL;
			desc._videoStream = NULL;
			desc._videoTimebase = 0.0;
			desc._eof = false;
            desc._audioThread = NULL;
            desc._videoThread = NULL;
			desc._demuxThread = NULL;
			desc._renderThread = NULL;
			desc._renderWindow = NULL;
			desc._renderRenderer = NULL;
			desc._renderTexture = NULL;
			if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 && error(&desc, -1))
                goto end;
            if (avformat_open_input(&desc._demuxFormatCtx, desc._impl->_filename.c_str(), NULL, NULL) != 0 && error(&desc, -2))
                goto end;
            if (avformat_find_stream_info(desc._demuxFormatCtx, NULL) < 0 && error(&desc, -3))
                goto end;
            for (int i = 0; i < (int)desc._demuxFormatCtx->nb_streams; i++)
            {
                if (desc._impl->_flag != NoAudio && desc._audioStream == NULL && desc._demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                    desc._audioStream = desc._demuxFormatCtx->streams[i];
                if (desc._impl->_flag != NoVideo && desc._videoStream == NULL && desc._demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                    desc._videoStream = desc._demuxFormatCtx->streams[i];
            }
            if (((desc._audioStream == NULL && desc._videoStream == NULL) || (desc._audioStream == NULL && desc._impl->_flag == NoVideo) ||
                 (desc._videoStream == NULL && desc._impl->_flag == NoAudio)) && error(&desc, -4))
                goto end;
			desc._timebase = CommonTimebase((desc._audioStream == NULL ? NULL : &desc._audioStream->time_base), (desc._videoStream == NULL ? NULL : &desc._videoStream->time_base));
            if (desc._audioStream != NULL)
            {
				desc._audioTimebase = desc._audioStream->time_base;
                AVCodec* audioCodec = avcodec_find_decoder(desc._audioStream->codecpar->codec_id);
                if (audioCodec == NULL && error(&desc, -5))
                    goto end;
                if ((desc._audioCodecCtx = avcodec_alloc_context3(audioCodec)) == NULL && error(&desc, -6))
                    goto end;
                if (avcodec_parameters_to_context(desc._audioCodecCtx, desc._audioStream->codecpar) != 0 && error(&desc, -7))
                    goto end;
                if (avcodec_open2(desc._audioCodecCtx, audioCodec, NULL) != 0 && error(&desc, -8))
                    goto end;
            }
            if (desc._videoStream != NULL)
            {
				desc._videoTimebase = desc._videoStream->time_base;
                AVCodec* videoCodec = avcodec_find_decoder(desc._videoStream->codecpar->codec_id);
                if (videoCodec == NULL && error(&desc, -9))
                    goto end;
                if ((desc._videoCodecCtx = avcodec_alloc_context3(videoCodec)) == NULL && error(&desc, -10))
                    goto end;
                if (avcodec_parameters_to_context(desc._videoCodecCtx, desc._videoStream->codecpar) != 0 && error(&desc, -11))
                    goto end;
                if (avcodec_open2(desc._videoCodecCtx, videoCodec, NULL) != 0 && error(&desc, -12))
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
                if (audioDeviceId == 0 && error(&desc, -13))
                    goto end;
                SDL_PauseAudioDevice(audioDeviceId, 0);
            }
			if (desc._videoStream)
			{
				int width = desc._videoCodecCtx->width;
				int height = desc._videoCodecCtx->height;
				desc._renderWindow = SDL_CreateWindow("MainWindow", 100, 100, width / 2, height / 2, SDL_WINDOW_SHOWN);
				if (desc._renderWindow == NULL && error(&desc, -14))
					goto end;
				desc._renderRenderer = SDL_CreateRenderer(desc._renderWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
				if (desc._renderRenderer == NULL && error(&desc, -15))
					goto end;
				desc._renderTexture = SDL_CreateTexture(desc._renderRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
				if (desc._renderTexture == NULL && error(&desc, -16))
					goto end;
			}

			if (desc._videoStream)
			{
//				double seekto = 4.0;
//				if (Seek(&desc, seekto) != 0 && error(&desc, -17))
//					goto end;
			}
			
			if ((desc._demuxThread = SDL_CreateThread(DemuxThread, "DemuxThread", &desc)) == NULL && error(&desc, -14))
				goto end;
			if (desc._audioStream && (desc._audioThread = SDL_CreateThread(AudioThread, "AudioThread", &desc)) == NULL && error(&desc, -15))
				goto end;
			if (desc._videoStream && (desc._videoThread = SDL_CreateThread(VideoThread, "VideoThread", &desc)) == NULL && error(&desc, -16))
				goto end;
			if (desc._videoStream && (desc._renderThread = SDL_CreateThread(RenderThread, "RenderThread", &desc)) == NULL && error(&desc, -17))
				goto end;

			desc.swstate(Playing);
			while (desc._impl->_looping)
            {
				SDL_Event event;
                if (SDL_PollEvent(&event))
                {
					switch (event.type)
                    {
					case SDL_KEYDOWN:
						if (event.key.keysym.sym == SDLK_LEFT)
						{
							desc.log("left\n");
							/*
							desc.swstate(Paused);
							for (AVPacket* packet = (AVPacket*)desc._videoPackets.Dequeue(); packet != NULL; av_packet_unref(packet), av_packet_free(&packet), packet = (AVPacket*)desc._videoPackets.Dequeue());
							for (AVPacket* packet = (AVPacket*)desc._audioPackets.Dequeue(); packet != NULL; av_packet_unref(packet), av_packet_free(&packet), packet = (AVPacket*)desc._audioPackets.Dequeue());
							for (AVFrame* frame = (AVFrame*)desc._videoFrames.Dequeue(); frame != NULL; av_frame_unref(frame), av_frame_free(&frame), frame = (AVFrame*)desc._videoFrames.Dequeue());
							for (AVFrame* frame = (AVFrame*)desc._audioFrames.Dequeue(); frame != NULL; av_frame_unref(frame), av_frame_free(&frame), frame = (AVFrame*)desc._audioFrames.Dequeue());
							
							double newtime = desc._impl->_time - 1.0;
							if (newtime < 0) newtime = 0;
							long long ts = (long long)(newtime / (desc._videoStream->time_base.num / (double)desc._videoStream->time_base.den));
							int ret = av_seek_frame(desc._demuxFormatCtx, desc._videoStream->index, ts, 0);
							if (ret != 0)
								break;
							desc._impl->_time = newtime;*/
						}
						else if (event.key.keysym.sym == SDLK_RIGHT)
						{
							desc.log("right\n");
						}
						else if (event.key.keysym.sym == SDLK_DOWN)
						{
							desc.log("down\n");
						}
						else if (event.key.keysym.sym == SDLK_UP)
						{
							desc.log("up\n");
						}
						else if (event.key.keysym.sym == SDLK_PAGEUP)
						{
							desc.log("pgup\n");
							if (desc._impl->_state == Paused)
								desc.swstate(Playing);
							else if (desc._impl->_state == Playing)
								desc.swstate(Paused);
						}
						break;
					case SDL_QUIT:
						desc._impl->_looping = false;
						continue;
                    };
                }
			    if (desc._impl->_state == Playing)
                {
					if ((desc._audioStream != NULL && desc._videoStream != NULL && desc._audioFrames.Size() == 0 && desc._videoFrames.Size() == 0) ||
						(desc._audioStream != NULL && desc._videoStream == NULL && desc._audioFrames.Size() == 0) || 
						(desc._audioStream == NULL && desc._videoStream != NULL && desc._videoFrames.Size() == 0))
					{
						if (desc._eof)
							desc.swstate(Paused);
						else
							desc.swstate(Buffering);
						continue;
					}
                }
				else if (desc._impl->_state == Buffering)
				{
					double endtime = desc._impl->_time + RENDER_BUFFERED;
					if ((desc._audioStream != NULL && desc._videoStream != NULL && desc._audioFrames.Tail() >= endtime && desc._videoFrames.Tail() >= endtime) ||
						(desc._audioStream != NULL && desc._videoStream == NULL && desc._audioFrames.Tail() >= endtime)||
						(desc._audioStream == NULL && desc._videoStream != NULL && desc._videoFrames.Tail() >= endtime))
					{
						desc.swstate(Playing);
						continue;
					}
				}
            }
		end:
			if (desc._renderThread != NULL) SDL_WaitThread(desc._renderThread, NULL);
			if (desc._demuxThread != NULL) SDL_WaitThread(desc._demuxThread, NULL);
            if (desc._videoThread != NULL) SDL_WaitThread(desc._videoThread, NULL);
            if (desc._audioThread != NULL) SDL_WaitThread(desc._audioThread, NULL);
            for (AVPacket* packet = (AVPacket*)desc._videoPackets.Dequeue(); packet != NULL; av_packet_unref(packet), av_packet_free(&packet), packet = (AVPacket*)desc._videoPackets.Dequeue());
            for (AVPacket* packet = (AVPacket*)desc._audioPackets.Dequeue(); packet != NULL; av_packet_unref(packet), av_packet_free(&packet), packet = (AVPacket*)desc._audioPackets.Dequeue());
            for (AVFrame* frame = (AVFrame*)desc._videoFrames.Dequeue(); frame != NULL; av_frame_unref(frame), av_frame_free(&frame), frame = (AVFrame*)desc._videoFrames.Dequeue());
            for (AVFrame* frame = (AVFrame*)desc._audioFrames.Dequeue(); frame != NULL; av_frame_unref(frame), av_frame_free(&frame), frame = (AVFrame*)desc._audioFrames.Dequeue());
            if (desc._audioCodecCtx != NULL) avcodec_close(desc._audioCodecCtx);
            if (desc._videoCodecCtx != NULL) avcodec_close(desc._videoCodecCtx);
            if (desc._demuxFormatCtx != NULL) avformat_close_input(&desc._demuxFormatCtx);
			if (desc._renderTexture != NULL) SDL_DestroyTexture(desc._renderTexture);
			if (desc._renderRenderer != NULL) SDL_RenderClear(desc._renderRenderer);
			if (desc._renderWindow != NULL) SDL_DestroyWindow(desc._renderWindow);
			desc._renderThread = NULL;
			desc._demuxThread = NULL;
            desc._videoThread = NULL;
            desc._audioThread = NULL;
            desc._demuxFormatCtx = NULL;
            desc._audioCodecCtx = NULL;
            desc._videoCodecCtx = NULL;
			desc._audioStream = NULL;
			desc._videoStream = NULL;
			desc._renderWindow = NULL;
			desc._renderRenderer = NULL;
			desc._renderTexture = NULL;
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
        _impl->_time = 0.0;
        _impl->_state = Closed;
        _impl->_ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
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

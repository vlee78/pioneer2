#pragma warning (disable:4819)

#include "SFPlayer.h"
#include "SFSync.h"
#include "SFUtils.h"
#include "SFMutex.h"
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
	class SFPlayer::SFPlayerImpl
	{
	public:
		AVFormatContext* _demuxFormatCtx;
		AVStream* _audioStream;
		AVStream* _videoStream;
		AVCodecContext* _audioCodecCtx;
		AVCodecContext* _videoCodecCtx;
		AVRational* _audioTimebase;
		AVRational* _videoTimebase;
		AVRational _commonTimebase;
		SFSync _sync;
		SDL_Window* _renderWindow;
		SDL_Renderer* _renderRenderer;
		SDL_Texture* _renderTexture;
		long long _timestamp;
		std::list<AVFrame*> _audioFrames;
		std::list<AVFrame*> _videoFrames;
		State _state;
		SFMutex _mutex;
		AVFrame* _audioFrame;
		AVFrame* _videoFrame;

		static void AudioDevice(void* userdata, Uint8* stream, int len)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)userdata;
			memset(stream, 0, len);
			int sampleRate = impl->_audioStream->codecpar->sample_rate;
			short* buffer0 = ((short*)stream) + 0;
			short* buffer1 = ((short*)stream) + 1;
			int bufchs = 2;
			int bufoff = 0;
			int bufmax = len / sizeof(short);

			AVFrame*& frame = impl->_audioFrame;
			int nb_samples = 0;
			while (impl->_sync.Test())
			{
				if (frame == NULL)
				{
					impl->_mutex.Enter();
					if (impl->_state == Playing && impl->_audioFrames.size() > 0)
					{
						frame = impl->_audioFrames.front();
						impl->_audioFrames.pop_front();
					}
					impl->_mutex.Leave();
				}
				if (frame == NULL)
					break;
				AVSampleFormat format = (AVSampleFormat)frame->format;
				int channels = frame->channels;
				nb_samples = frame->nb_samples;
				if (sampleRate != frame->sample_rate && impl->_sync.Error(-21, ""))
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
						impl->_sync.Error(-23, "");
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
						impl->_sync.Error(-24, "");
						break;
					}
				}
				else
				{
					impl->_sync.Error(-25, "");
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
				long long samples = bufoff / bufchs;//总共消耗了frame queue多少sample
				long long shift = SFUtils::SamplesToTimestamp(samples, sampleRate, &impl->_commonTimebase);
				impl->_mutex.Enter();
				impl->_timestamp += shift;
				impl->_mutex.Leave();
			}
		}

		static void RenderThread(SFSync* sync, SFThread* thread, void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			while (sync->Loop(thread))
			{
			}
		}

		static void DecodeThread(SFSync* sync, SFThread* thread, void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			AVPacket* packet = NULL;
			AVFrame* audioFrame = NULL;
			AVFrame* videoFrame = NULL;
			while (sync->Loop(thread))
			{
				impl->_mutex.Enter();
				long long thresholdTimestamp = impl->_timestamp + SFUtils::SecondsToTimestamp(5.0, &impl->_commonTimebase);
				long long audioTailTimestamp = (impl->_audioStream == NULL ? LLONG_MAX : (impl->_audioFrames.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_audioFrames.back()->pts, impl->_audioTimebase, &impl->_commonTimebase)));
				long long videoTailTimestamp = (impl->_videoStream == NULL ? LLONG_MAX : (impl->_videoFrames.size() == 0 ? -1 : SFUtils::TimestampToTimestamp(impl->_videoFrames.back()->pts, impl->_videoTimebase, &impl->_commonTimebase)));
				if (audioTailTimestamp >= thresholdTimestamp && videoTailTimestamp >= thresholdTimestamp)
				{
					if (impl->_state == Buffering)
						impl->_state = Playing;
					impl->_mutex.Leave();
					SDL_Delay(5);
					continue;
				}
				impl->_mutex.Leave();
				if (packet == NULL && (packet = av_packet_alloc()) == NULL && sync->Error(-11, ""))
					continue;
				int ret = av_read_frame(impl->_demuxFormatCtx, packet);
				if (ret == AVERROR_EOF)
				{
					impl->_mutex.Enter();
					impl->_state = Eof;
					impl->_mutex.Leave();
					SDL_Delay(5);
					continue;
				}
				else if (ret != 0 && sync->Error(-12, ""))
					continue;
				if (impl->_audioStream && packet->stream_index == impl->_audioStream->index)
				{
					if (avcodec_send_packet(impl->_audioCodecCtx, packet) != 0 && sync->Error(-13, ""))
						continue;
					while (true)
					{
						if (audioFrame == NULL && (audioFrame = av_frame_alloc()) == NULL && sync->Error(-14, ""))
							break;
						ret = avcodec_receive_frame(impl->_audioCodecCtx, audioFrame);
						if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
							break;
						else if (ret < 0 && sync->Error(-15, ""))
							break;
						long long timestamp = SFUtils::TimestampToTimestamp(audioFrame->pts, impl->_audioTimebase, &impl->_commonTimebase);
						if (timestamp >= impl->_timestamp)
						{
							impl->_mutex.Enter();
							impl->_audioFrames.push_back(audioFrame);
							impl->_mutex.Leave();
							audioFrame = NULL;
						}
						else
						{
							av_frame_unref(audioFrame);
							av_frame_free(&audioFrame);
							audioFrame = NULL;
						}
					}
					SDL_Delay(1);
				}
				else if (impl->_videoStream && packet->stream_index == impl->_videoStream->index)
				{
					if (avcodec_send_packet(impl->_videoCodecCtx, packet) != 0 && sync->Error(-16, ""))
						continue;
					while (true)
					{
						if (videoFrame == NULL && (videoFrame = av_frame_alloc()) == NULL && sync->Error(-17, ""))
							break;
						int ret = avcodec_receive_frame(impl->_videoCodecCtx, videoFrame);
						if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
							break;
						if (ret < 0 && sync->Error(-18, ""))
							break;
						long long timestamp = SFUtils::TimestampToTimestamp(videoFrame->pts, impl->_videoTimebase, &impl->_commonTimebase);
						if (timestamp >= impl->_timestamp)
						{
							impl->_mutex.Enter();
							impl->_videoFrames.push_back(videoFrame);
							impl->_mutex.Leave();
							videoFrame = NULL;
						}
						else
						{
							av_frame_unref(videoFrame);
							av_frame_free(&videoFrame);
							videoFrame = NULL;
						}
					}
					SDL_Delay(1);
				}
				av_packet_unref(packet);
				av_packet_free(&packet);
				packet = NULL;
			}
			if (audioFrame != NULL)
			{
				av_frame_unref(audioFrame);
				av_frame_free(&audioFrame);
				audioFrame = NULL;
			}
			if (videoFrame != NULL)
			{
				av_frame_unref(videoFrame);
				av_frame_free(&videoFrame);
				videoFrame = NULL;
			}
		}

		static void PollThread(SFSync* sync, SFThread* thread, void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			SFMsg msg;
			while (sync->Poll(thread, msg))
			{
			}
		}

		static void MainThread(SFSync* sync, SFThread* thread, void* param)
		{
			SFPlayerImpl* impl = (SFPlayerImpl*)param;
			if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 && sync->Error(-1, ""))
				goto end;
			if (impl->_audioStream)
			{
				SDL_AudioSpec want;
				SDL_zero(want);
				want.freq = impl->_audioStream->codecpar->sample_rate;
				want.format = AUDIO_S16SYS;
				want.channels = 2;
				want.samples = 1024;// impl->_decoder.GetAudioFrameSize();
				want.callback = SFPlayerImpl::AudioDevice;
				want.userdata = impl;
				want.padding = 52428;
				SDL_AudioSpec real;
				SDL_zero(real);
				SDL_AudioDeviceID audioDeviceId = SDL_OpenAudioDevice(NULL, 0, &want, &real, 0);
				if (audioDeviceId == 0 && sync->Error(-2, ""))
					goto end;
				SDL_PauseAudioDevice(audioDeviceId, 0);
			}
			if (impl->_videoStream)
			{
				int width = impl->_videoStream->codecpar[impl->_videoStream->index].width;
				int height = impl->_videoStream->codecpar[impl->_videoStream->index].height;
				impl->_renderWindow = SDL_CreateWindow("RenderWindow", 100, 100, width, height, SDL_WINDOW_SHOWN);
				if (impl->_renderWindow == NULL && sync->Error(-3, ""))
					goto end;
				impl->_renderRenderer = SDL_CreateRenderer(impl->_renderWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
				if (impl->_renderRenderer == NULL && sync->Error(-4, ""))
					goto end;
				impl->_renderTexture = SDL_CreateTexture(impl->_renderRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
				if (impl->_renderTexture == NULL && sync->Error(-5, ""))
					goto end;
			}
			if (impl->_videoStream && sync->Spawn("RenderThread", SFPlayerImpl::RenderThread, impl) == false && sync->Error(-6, ""))
				goto end;
			if (sync->Spawn("DecodeThread", SFPlayerImpl::DecodeThread, impl) == false && sync->Error(-7, ""))
				goto end;
			if (sync->Spawn("PollThread", SFPlayerImpl::PollThread, impl) == false && sync->Error(-8, ""))
				goto end;
			while (sync->Loop(thread))
			{
				SDL_Event event;
				if (SDL_PollEvent(&event))
				{
					switch (event.type)
					{
					case SDL_QUIT:
						sync->Term();
						break;
					};
				}
			}
		end:
			if (impl->_renderTexture != NULL) SDL_DestroyTexture(impl->_renderTexture);
			if (impl->_renderRenderer != NULL) SDL_RenderClear(impl->_renderRenderer);
			if (impl->_renderWindow != NULL) SDL_DestroyWindow(impl->_renderWindow);
			impl->_renderWindow = NULL;
			impl->_renderRenderer = NULL;
			SDL_Quit();
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
		_impl->_demuxFormatCtx = NULL;
		_impl->_audioStream = NULL;
		_impl->_videoStream = NULL;
		_impl->_audioCodecCtx = NULL;
		_impl->_videoCodecCtx = NULL;
		_impl->_audioTimebase = NULL;
		_impl->_videoTimebase = NULL;
		_impl->_commonTimebase = { 0, 0 };
		_impl->_renderWindow = NULL;
		_impl->_renderRenderer = NULL;
		_impl->_renderTexture = NULL;
		_impl->_timestamp = 0;
		_impl->_audioFrames.clear();
		_impl->_videoFrames.clear();
		_impl->_state = Closed;
		_impl->_audioFrame = NULL;
		_impl->_videoFrame = NULL;

		if (avformat_open_input(&_impl->_demuxFormatCtx, filename, NULL, NULL) != 0 && Uninit())
			return -2;
		if (avformat_find_stream_info(_impl->_demuxFormatCtx, NULL) < 0 && Uninit())
			return -3;
		for (int i = 0; i < (int)_impl->_demuxFormatCtx->nb_streams; i++)
		{
			if (flag != NoAudio && _impl->_audioStream == NULL && _impl->_demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
				_impl->_audioStream = _impl->_demuxFormatCtx->streams[i];
			if (flag != NoVideo && _impl->_videoStream == NULL && _impl->_demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
				_impl->_videoStream = _impl->_demuxFormatCtx->streams[i];
		}
		if (((_impl->_audioStream == NULL && _impl->_videoStream == NULL) || (_impl->_audioStream == NULL && flag == NoVideo) || (_impl->_videoStream == NULL && flag == NoAudio)) && Uninit())
			return -4;
		if (_impl->_audioStream != NULL)
		{
			_impl->_audioTimebase = &_impl->_audioStream->time_base;
			AVCodec* audioCodec = avcodec_find_decoder(_impl->_audioStream->codecpar->codec_id);
			if (audioCodec == NULL && Uninit())
				return -5;
			if ((_impl->_audioCodecCtx = avcodec_alloc_context3(audioCodec)) == NULL && Uninit())
				return -6;
			if (avcodec_parameters_to_context(_impl->_audioCodecCtx, _impl->_audioStream->codecpar) != 0 && Uninit())
				return -7;
			if (avcodec_open2(_impl->_audioCodecCtx, audioCodec, NULL) != 0 && Uninit())
				return -8;
		}
		if (_impl->_videoStream != NULL)
		{
			_impl->_videoTimebase = &_impl->_videoStream->time_base;
			AVCodec* videoCodec = avcodec_find_decoder(_impl->_videoStream->codecpar->codec_id);
			if (videoCodec == NULL && Uninit())
				return -9;
			if ((_impl->_videoCodecCtx = avcodec_alloc_context3(videoCodec)) == NULL && Uninit())
				return -10;
			if (avcodec_parameters_to_context(_impl->_videoCodecCtx, _impl->_videoStream->codecpar) != 0 && Uninit())
				return -11;
			if (avcodec_open2(_impl->_videoCodecCtx, videoCodec, NULL) != 0 && Uninit())
				return -12;
		}
		_impl->_state = Buffering;
		_impl->_commonTimebase = SFUtils::CommonTimebase(_impl->_audioTimebase, _impl->_videoTimebase);
		if (_impl->_sync.Init() == false && Uninit())
			return -13;
		if (_impl->_sync.Spawn("MainThread", SFPlayerImpl::MainThread, _impl) == false && Uninit())
			return -14;
		return 0;
	}

	bool SFPlayer::Uninit()
	{
		if (_impl != NULL)
		{
			_impl->_sync.Uninit();
			for (auto it = _impl->_audioFrames.begin(); it != _impl->_audioFrames.end(); it = _impl->_audioFrames.erase(it))
			{
				av_frame_unref(*it);
				av_frame_free(&*it);
			}
			for (auto it = _impl->_videoFrames.begin(); it != _impl->_videoFrames.end(); it = _impl->_videoFrames.erase(it))
			{
				av_frame_unref(*it);
				av_frame_free(&*it);
			}
			if (_impl->_audioFrame != NULL)
			{
				av_frame_unref(_impl->_audioFrame);
				av_frame_free(&_impl->_audioFrame);
				_impl->_audioFrame = NULL;
			}
			if (_impl->_videoFrame != NULL)
			{
				av_frame_unref(_impl->_videoFrame);
				av_frame_free(&_impl->_videoFrame);
				_impl->_videoFrame = NULL;
			}
			if (_impl->_audioCodecCtx != NULL) avcodec_close(_impl->_audioCodecCtx);
			if (_impl->_videoCodecCtx != NULL) avcodec_close(_impl->_videoCodecCtx);
			if (_impl->_demuxFormatCtx != NULL) avformat_close_input(&_impl->_demuxFormatCtx);
			_impl->_renderTexture = NULL;
			_impl->_renderRenderer = NULL;
			_impl->_renderWindow = NULL;
			_impl->_commonTimebase = { 0,0 };
			_impl->_audioTimebase = NULL;
			_impl->_videoTimebase = NULL;
			_impl->_audioStream = NULL;
			_impl->_videoStream = NULL;
			_impl->_audioCodecCtx = NULL;
			_impl->_videoCodecCtx = NULL;
			_impl->_demuxFormatCtx = NULL;
			delete _impl;
			_impl = NULL;
		}
		return true;
	}

#ifdef _SSS_
    static const char* StateNames[] =
    {
        "Closed(0)",
        "Paused(1)",
        "Buffering(2)",
        "Playing(3)",
    };

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

	static long long TimestampToTimestamp(long long timestamp, AVRational* frTimebase, AVRational* toTimebase)
	{
		return timestamp * frTimebase->num * toTimebase->den / (frTimebase->den * toTimebase->num);
	}

	static long long SecondsToTimestamp(double seconds, AVRational* timebase)
	{
		return (long long)(seconds * timebase->den / timebase->num);
	}

	static double TimestampToSeconds(long long timestamp, AVRational* timebase)
	{
		return timestamp * timebase->num / (double)timebase->den;
	}

	static long long SamplesToTimestamp(long long samples, int sampleRate, AVRational* timebase)
	{
		return (samples * timebase->den) / ((long long)sampleRate * timebase->num);
	}

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

		long long Tail()
		{
			SDL_LockMutex(_mutex);
			long long tail = 0;
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
				head = frame->pts + frame->height;//偏移的ts
			}
			SDL_UnlockMutex(_mutex);
			return head;
		}

		long long Tail()
		{
			SDL_LockMutex(_mutex);
			long long tail = 0;
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
			AVRational* _audioTimebase;
            PacketQueue _audioPackets;
            FrameQueue _audioFrames;
			AVCodecContext* _videoCodecCtx;
			AVStream* _videoStream;
			AVRational* _videoTimebase;
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
				fprintf(file, "(%.3f, %.3f) A:[%.3f, %.3f] V:[%.3f, %.3f] a:[%.3f, %.3f] v:[%.3f, %.3f] \n", 
					time, TimestampToSeconds(_impl->_time, &_timebase), 
					TimestampToSeconds(_audioFrames.Head(), _audioTimebase), TimestampToSeconds(_audioFrames.Tail(), _audioTimebase), 
					TimestampToSeconds(_videoFrames.Head(), _videoTimebase), TimestampToSeconds(_videoFrames.Tail(), _videoTimebase),
					TimestampToSeconds(_audioPackets.Head(), _audioTimebase), TimestampToSeconds(_audioPackets.Tail(), _audioTimebase),
					TimestampToSeconds(_videoPackets.Head(), _videoTimebase), TimestampToSeconds(_videoPackets.Tail(), _videoTimebase));
				printf("(%.3f, %.3f) A:[%.3f, %.3f] V:[%.3f, %.3f] a:[%.3f, %.3f] v:[%.3f, %.3f] \n", 
					time, TimestampToSeconds(_impl->_time, &_timebase),
					TimestampToSeconds(_audioFrames.Head(), _audioTimebase), TimestampToSeconds(_audioFrames.Tail(), _audioTimebase),
					TimestampToSeconds(_videoFrames.Head(), _videoTimebase), TimestampToSeconds(_videoFrames.Tail(), _videoTimebase),
					TimestampToSeconds(_audioPackets.Head(), _audioTimebase), TimestampToSeconds(_audioPackets.Tail(), _audioTimebase),
					TimestampToSeconds(_videoPackets.Head(), _videoTimebase), TimestampToSeconds(_videoPackets.Tail(), _videoTimebase));
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
				int channels = frame->channels;
                if (sampleRate != frame->sample_rate || format != AV_SAMPLE_FMT_FLTP)
                {
                    desc->_impl->_errorCode = -201;
                    desc->_impl->_looping = false;
                    return;
                }
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
				frame->height = (int)SamplesToTimestamp(frameoff, sampleRate, desc->_audioTimebase);
				if (frameoff >= framemax)
					desc->_audioFrames.PopFront();
                if (bufoff >= bufmax)
                    break;
            }
            long long samples = bufoff / bufchs;//总共消耗了frame queue多少sample
			long long shift = SamplesToTimestamp(samples, sampleRate, &desc->_timebase);
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
					SDL_Delay(0);
					continue;
				}
				AVFrame* frame = (AVFrame*)desc->_videoFrames.PeekFront();
				if (frame != NULL)
				{
					long long time = TimestampToTimestamp(frame->best_effort_timestamp, desc->_videoTimebase, &desc->_timebase);
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
				{/*
					long long nanoTs = std::chrono::high_resolution_clock::now().time_since_epoch().count();
					long long nanoDiff = nanoTs - desc->_impl->_ts;
					double diffSecs = nanoDiff / (double)1000000000l;
					if (nanoDiff > 0)
					{
						desc->_impl->_time += diffSecs;
						desc->_impl->_ts = nanoTs;
					}*/
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
				long long endtime = desc->_impl->_time + SecondsToTimestamp(AUDIO_SATURATE, &desc->_timebase);
                if ((desc->_impl->_state != Buffering && desc->_impl->_state != Playing) ||
                    (TimestampToTimestamp(desc->_audioFrames.Tail(), desc->_audioTimebase, &desc->_timebase) >= endtime ) ||
                    (packet = (AVPacket*)desc->_audioPackets.Dequeue()) == NULL)
                {//如果A的Frames队列尾时间超过time+sat,暂停decode
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
					long long time = TimestampToTimestamp(frame->pts, desc->_audioTimebase, &desc->_timebase);
					if (time >= desc->_impl->_time)
					{//解压包时间戳大于等于当前时间才进入render对列
						frame->width = 0;
						frame->height = 0;
						desc->_audioFrames.Enqueue(frame);
					}
					else
					{//否则丢弃掉
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
				long long endtime = desc->_impl->_time + SecondsToTimestamp(VIDEO_SATURATE, &desc->_timebase);
				if ((desc->_impl->_state != Buffering && desc->_impl->_state != Playing) ||
                    (TimestampToTimestamp(desc->_videoFrames.Tail(), desc->_videoTimebase, &desc->_timebase) >= endtime ) ||
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
					long long time = TimestampToTimestamp(frame->pts, desc->_videoTimebase, &desc->_timebase);
					if (time >= desc->_impl->_time)
					{
						desc->_videoFrames.Enqueue(frame);
						frame = NULL;
					}
					else
					{
						av_frame_unref(frame);
						av_frame_free(&frame);
						frame = NULL;
					}
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
				long long endtime = desc->_impl->_time + SecondsToTimestamp(DEMUX_SATURATE, &desc->_timebase);
				if ((desc->_videoStream != NULL && desc->_audioStream != NULL && 	
						TimestampToTimestamp(desc->_videoPackets.Tail(), desc->_videoTimebase, &desc->_timebase) >= endtime && 
						TimestampToTimestamp(desc->_audioPackets.Tail(), desc->_audioTimebase, &desc->_timebase) >= endtime) ||
					(desc->_audioStream != NULL && desc->_videoStream == NULL &&
						TimestampToTimestamp(desc->_audioPackets.Tail(), desc->_audioTimebase, &desc->_timebase) >= endtime) ||
					(desc->_audioStream == NULL && desc->_videoStream != NULL &&
						TimestampToTimestamp(desc->_videoPackets.Tail(), desc->_videoTimebase, &desc->_timebase) >= endtime))
				{//如果v或者a的packet尾都超过当前+sat则停止demux进程,这里要考虑eof的情况,也要考虑v/a不同步的极端情况
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
						desc->_audioPackets.Enqueue(packet);
						packet = NULL;
					}
					else if (desc->_videoStream && packet->stream_index == desc->_videoStream->index)
					{
						desc->_videoPackets.Enqueue(packet);
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
		{/*
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
			return errorCode;*/
			return 0;
		}
        
        static int MainThread(void* param)
        {
			Desc desc;
			desc._impl = (SFPlayerImpl*)param;
			desc._demuxFormatCtx = NULL;
			desc._audioCodecCtx = NULL;
			desc._audioStream = NULL;
			desc._audioTimebase = NULL;
			desc._videoCodecCtx = NULL;
			desc._videoStream = NULL;
			desc._videoTimebase = NULL;
			desc._timebase = { 0, 0 };
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
				desc._audioTimebase = &desc._audioStream->time_base;
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
				desc._videoTimebase = &desc._videoStream->time_base;
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
					long long endtime = desc._impl->_time + SecondsToTimestamp(RENDER_BUFFERED, &desc._timebase);
					if ((desc._audioStream != NULL && desc._videoStream != NULL && 
							TimestampToTimestamp(desc._audioFrames.Tail(), desc._audioTimebase, &desc._timebase) >= endtime && 
							TimestampToTimestamp(desc._videoFrames.Tail(), desc._videoTimebase, &desc._timebase) >= endtime) ||
						(desc._audioStream != NULL && desc._videoStream == NULL && 
							TimestampToTimestamp(desc._audioFrames.Tail(), desc._audioTimebase, &desc._timebase) >= endtime)||
						(desc._audioStream == NULL && desc._videoStream != NULL && 
							TimestampToTimestamp(desc._videoFrames.Tail(), desc._videoTimebase, &desc._timebase) >= endtime))
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
        _impl->_time = 0;
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
#endif
}

#pragma warning (disable:4819)

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

#define DEMUX_SATURATE 5//A和V的packet队列尾同时大于time+DEMUX_SATURATE的话，暂停demux
#define AUDIO_SATURATE 5//A的frame队列尾大于time+AUDIO_SATURATE的话，暂停audio解码
#define VIDEO_SATURATE 5//V的frame队列尾大于time+VIDEO_SATURATE的话，暂停video解码
#define RENDER_BUFFERED 4//A和V的frame队列尾大于time+RENDER_BUFFERED的话，从buffering开始playing

namespace pioneer
{
	class SFDecoder::SFDecoderImpl
	{
	public:
		std::string _filename;
		AVFormatContext* _demuxFormatCtx;
		AVStream* _audioStream;
		AVStream* _videoStream;
		AVCodecContext* _audioCodecCtx;
		AVCodecContext* _videoCodecCtx;
		AVRational _commonTimebase;
		AVRational _audioTimebase;
		AVRational _videoTimebase;
		bool _looping;
		SDL_Thread* _thread;

		static int DecodeThread(void* param)
		{
			return 0;
		}
	};

	SFDecoder::SFDecoder()
	{
		_impl = NULL;
	}

	SFDecoder::~SFDecoder()
	{
		Uninit();
	}

	long long SFDecoder::Init(const char* filename, Flag flag)
	{
		Uninit();
		_impl = new(std::nothrow) SFDecoderImpl();
		if (_impl == NULL)
			return -1;
		_impl->_filename = filename;
		_impl->_demuxFormatCtx = NULL;
		_impl->_audioStream = NULL;
		_impl->_videoStream = NULL;
		_impl->_audioCodecCtx = NULL;
		_impl->_videoCodecCtx = NULL;
		_impl->_commonTimebase = { 0, 0 };
		_impl->_audioTimebase = { 0, 0 };
		_impl->_videoTimebase = { 0, 0 };
		_impl->_looping = true;
		_impl->_thread = NULL;

		if (avformat_open_input(&_impl->_demuxFormatCtx, _impl->_filename.c_str(), NULL, NULL) != 0 && Uninit())
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
		_impl->_commonTimebase = SFUtils::CommonTimebase((_impl->_audioStream == NULL ? NULL : &_impl->_audioStream->time_base), (_impl->_videoStream == NULL ? NULL : &_impl->_videoStream->time_base));
		if (_impl->_audioStream != NULL)
		{
			_impl->_audioTimebase = _impl->_audioStream->time_base;
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
			_impl->_videoTimebase = _impl->_videoStream->time_base;
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
		SDL_Thread* thread = SDL_CreateThread(SFDecoderImpl::DecodeThread, "Decoder", _impl);
		if (thread == NULL && Uninit())
			return -13;
        return 0;
	}

	bool SFDecoder::Uninit()
	{
		if (_impl != NULL)
		{
			_impl->_looping = false;
			if (_impl->_thread != NULL)
			{
				SDL_WaitThread(_impl->_thread, NULL);
				_impl->_thread = NULL;
			}
			delete _impl;
			_impl = NULL;
		}
		return true;
	}
}

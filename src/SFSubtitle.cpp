#pragma warning (disable:4819)

#include "SFSubtitle.h"

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
	long long SFSubtitle::ProcessSubtitle(const char* filepath)
	{
		AVFormatContext* demuxFormatCtx = NULL;
		if (avformat_open_input(&demuxFormatCtx, filepath, NULL, NULL) != 0)
			return -1;
		if (avformat_find_stream_info(demuxFormatCtx, NULL) < 0)
			return -2;
		av_dump_format(demuxFormatCtx, 0, filepath, 0);
		AVStream* stream = NULL;
		for (int i = 0; i < (int)demuxFormatCtx->nb_streams; i++)
		{
			if (demuxFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
			{
				stream = demuxFormatCtx->streams[i];
				break;
			}
		}
		if (stream == NULL)
			return -3;

		AVPacket* packet = NULL;
		while (true)
		{
			if (packet == NULL && (packet = av_packet_alloc()) == NULL)
				return -4;
			int ret = av_read_frame(demuxFormatCtx, packet);
			if (ret == AVERROR_EOF)
				break;
			else if (ret == AVERROR(EAGAIN))
				continue;
			else if (ret != 0)
				return -5;
			if (packet->stream_index == stream->index)
			{
				int pts = packet->dts;
			}
			av_packet_unref(packet);
			av_packet_free(&packet);
		}

		return 0;
	}
}

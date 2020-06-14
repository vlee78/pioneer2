#include "Pioneer.h"
#include <stdio.h>
#include <string>
#include <iostream>


extern "C" 
{
	#include "libavformat/avformat.h"
	#include "libavcodec/avcodec.h"

}

namespace pioneer
{
	static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame,
		FILE *outfile)
	{
		int i, ch;
		int ret, data_size;

		/* send the packet with the compressed data to the decoder */
		ret = avcodec_send_packet(dec_ctx, pkt);
		if (ret < 0) {
			fprintf(stderr, "Error submitting the packet to the decoder\n");
			exit(1);
		}

		/* read all the output frames (in general there may be any number of them */
		while (ret >= 0) {
			ret = avcodec_receive_frame(dec_ctx, frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				return;
			else if (ret < 0) {
				fprintf(stderr, "Error during decoding\n");
				exit(1);
			}
			data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
			if (data_size < 0) {
				/* This should not occur, checking just for paranoia */
				fprintf(stderr, "Failed to calculate data size\n");
				exit(1);
			}
			for (i = 0; i < frame->nb_samples; i++)
				for (ch = 0; ch < dec_ctx->channels; ch++)
					fwrite(frame->data[ch] + data_size*i, 1, data_size, outfile);
		}
	}

	int Pioneer::testMediaInfo(int argc, const char* argv[])
	{
		const char* filename = "test.mov";
		AVFormatContext* fmt_ctx = NULL;
		int ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL);
		if (ret != 0)
			return ret;
		ret = avformat_find_stream_info(fmt_ctx, NULL);
		if (ret != 0) 
		{
			printf("Cannot find stream information\n");
			avformat_close_input(&fmt_ctx);
			return ret;
		}
		AVDictionaryEntry *tag = NULL;
		while (true)
		{
			tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX);
			if (tag == NULL)
				break;
			printf("%s=%s\n", tag->key, tag->value);
		}			
		avformat_close_input(&fmt_ctx);
		return 0;
	}

	int Pioneer::testDecodeMp2(int argc, const char* argv[])
	{
		const char* filename = "sample1.mp2";
		const char* outfilename = "sample1.pcm";
		AVPacket* pkt = av_packet_alloc();
		const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_MP3);
		if (codec == NULL) 
		{
			av_packet_free(&pkt);
			printf("Codec not found\n");
			return -1;
		}

		AVCodecParserContext* parser = av_parser_init(codec->id);
		if (parser == NULL) 
		{
			av_packet_free(&pkt);
			printf("Parser not found\n");
			return -2;
		}

		AVCodecContext* context = avcodec_alloc_context3(codec);
		if (context == NULL) 
		{
			av_parser_close(parser);
			av_packet_free(&pkt);
			printf("Could not allocate audio codec context\n");
			return -3;
		}

		if (avcodec_open2(context, codec, NULL) < 0) 
		{
			avcodec_free_context(&context);
			av_parser_close(parser);
			av_packet_free(&pkt);
			printf("Could not open codec\n");
			return -4;
		}

		FILE* f = fopen(filename, "rb");
		if (f == NULL) 
		{
			avcodec_free_context(&context);
			av_parser_close(parser);
			av_packet_free(&pkt);
			printf("Could not open %s\n", filename);
			return -5;
		}

		FILE* outfile = fopen(outfilename, "wb");
		if (outfile = NULL) 
		{
			fclose(f);
			avcodec_free_context(&context);
			av_parser_close(parser);
			av_packet_free(&pkt);
			printf("Could not open %s\n", outfilename);
			return -6;
		}

		#define AUDIO_INBUF_SIZE 20480
		#define AUDIO_REFILL_THRESH 4096
		uint8_t inbuf[AUDIO_INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
		uint8_t* data = inbuf;

		size_t data_size = fread(inbuf, 1, AUDIO_INBUF_SIZE, f);
		
		AVFrame* decoded_frame = av_frame_alloc();
		if (decoded_frame == NULL)
		{
			fclose(f);
			fclose(outfile);
			av_frame_free(&decoded_frame);
			avcodec_free_context(&context);
			av_parser_close(parser);
			av_packet_free(&pkt);
			printf("Could not allocate audio frame\n");
			return -6;
		}

		while (data_size > 0) 
		{
			int ret = av_parser_parse2(parser, context, &pkt->data, &pkt->size, data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
			if (ret < 0) 
			{
				fclose(f);
				fclose(outfile);
				av_frame_free(&decoded_frame);
				avcodec_free_context(&context);
				av_parser_close(parser);
				av_packet_free(&pkt);
				fprintf(stderr, "Error while parsing\n");
				exit(1);
			}
			data += ret;
			data_size -= ret;

			if (pkt->size)
				decode(context, pkt, decoded_frame, outfile);

			if (data_size < AUDIO_REFILL_THRESH) 
			{
				memmove(inbuf, data, data_size);
				data = inbuf;
				int len = fread(data + data_size, 1, AUDIO_INBUF_SIZE - data_size, f);
				if (len > 0)
					data_size += len;
			}
		}

		pkt->data = NULL;
		pkt->size = 0;
		decode(context, pkt, decoded_frame, outfile);

		fclose(f);
		fclose(outfile);
		av_frame_free(&decoded_frame);
		avcodec_free_context(&context);
		av_parser_close(parser);
		av_packet_free(&pkt);
		return 0;
	}

	int Pioneer::testDecodeAudio(int argc, const char* argv[])
	{
		av_register_all();

		const char* infile_name = "mojito.mp3";
		const char* outfile_name = "mojito.pcm";
		FILE* outfile = fopen(outfile_name, "wb");
		if (outfile == NULL)
		{
			printf("open %s failed\n", outfile);
			return -1;
		}

		AVFormatContext *pFormatCtx = NULL;
		int ret = avformat_open_input(&pFormatCtx, infile_name, NULL, NULL);
		if (ret < 0)
		{
			fclose(outfile);
			printf("avformat_open_input open %s failed\n", infile_name);
			return -2;
		}

		ret = avformat_find_stream_info(pFormatCtx, NULL);
		if (ret < 0)
		{
			fclose(outfile);
			avformat_close_input(&pFormatCtx);
			printf("Couldn't find stream information in file %s\n", infile_name);
			return -3;
		}
		av_dump_format(pFormatCtx, 0, infile_name, false);

		int audioStreamID = -1;
		for (int i = 0; i<pFormatCtx->nb_streams; i++) 
		{
			if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) 
			{
				audioStreamID = i;
				break;
			}
		}
		if (audioStreamID == -1) 
		{
			fclose(outfile);
			avformat_close_input(&pFormatCtx);
			printf("Didn't find a audio stream in file %s\n", infile_name);
			return -4;
		}

		AVCodecContext* pCodecCtx = pFormatCtx->streams[audioStreamID]->codec;

		AVCodec* pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
		if (pCodec == NULL)
		{
			printf("Codec type %d  not found\n", pCodecCtx->codec_id);
			return -5;
		}
		if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
		{
			avcodec_close(pCodecCtx);
			printf("Could not open codec\n");
			return -6;
		}
		
		static AVPacket packet;
		AVFrame* frame = av_frame_alloc();
		bool end_of_stream = false;
		while(true)
		{
			do 
			{
				if (packet.data != NULL)
					av_free_packet(&packet);

				if (av_read_frame(pFormatCtx, &packet)<0) 
				{
					end_of_stream = true;
					break;
				}
			} while (packet.stream_index != audioStreamID);
			// here, a new audio packet from the stream is available

			if (end_of_stream)
				break;

			printf("packetsize = %d\n", packet.size);

			ret = avcodec_send_packet(pCodecCtx, &packet);
			if (ret < 0) 
			{
				fprintf(stderr, "Error submitting the packet to the decoder\n");
				exit(1);
			}

			ret = avcodec_receive_frame(pCodecCtx, frame);
			if (ret < 0) 
			{
				fprintf(stderr, "Error during decoding\n");
				exit(1);
			}
			int data_size = av_get_bytes_per_sample(pCodecCtx->sample_fmt);
			if (data_size < 0) 
			{
				fprintf(stderr, "Failed to calculate data size\n");
				exit(1);
			}
			for (int i = 0; i < frame->nb_samples; i++)
			{
				for (int ch = 0; ch < pCodecCtx->channels; ch++)
					fwrite(frame->data[ch] + data_size*i, 1, data_size, outfile);
			}
		};

		if (packet.data != NULL)
			av_free_packet(&packet);
		avcodec_close(pCodecCtx);
		avformat_close_input(&pFormatCtx);
		fclose(outfile);
		return 0;
	}

	int Pioneer::testPioneer(int argc, const char* argv[])
	{
		//return testMediaInfo(argc, argv);
		//return testDecodeMp2(argc, argv);
		return testDecodeAudio(argc, argv);
	}
}
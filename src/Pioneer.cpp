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

		const char* infile_name =  "california.mkv";// "mojito.mp3";
		const char* outfile_name = "california.pcm";// "mojito.pcm";
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

	int Pioneer::testDecodeVideo2(int argc, const char* argv[])
	{
		av_register_all();

		const char* infile_name = "california.mkv";// "mojito.mp3";
		const char* outfile_name = "california.out.yuv";// "mojito.pcm";
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
			if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
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
		av_init_packet(&packet);
		while (true)
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


			int got_picture = 0;
			ret = avcodec_decode_video2(pCodecCtx, frame, &got_picture, &packet);
			if (ret < 0) {
				printf("Decode Error.\n");
				return -1;
			}
			static int fi = 0;
			int hit = 600;
			if (got_picture)
			{
				switch (pCodecCtx->pix_fmt)
				{
					case AV_PIX_FMT_YUV422P:
					{
						int index = 0;
						int y_i = 0, u_i = 0, v_i = 0;
						for (index = 0; index < frame->width*frame->height * 2;)
						{
							//outputframe[index++] = frame->data[0][y_i++];
							//outputframe[index++] = frame->data[1][u_i++];
							//outputframe[index++] = frame->data[0][y_i++];
							//outputframe[index++] = frame->data[2][v_i++];
						}
					}break;
					case AV_PIX_FMT_YUV420P:
					{
						
						fi++;
						//int bts = av_get_bytes_per_sample(pCodecCtx->pix_fmt);
						uint8_t* data0 = frame->data[0];
						if (fi>=hit) fwrite(data0, frame->width*frame->height, 1, outfile);

						uint8_t* data1 = frame->data[1];
						if (fi >= hit) fwrite(data1, frame->width/2 * frame->height/2, 1, outfile);

						uint8_t* data2 = frame->data[2];
						if (fi >= hit) fwrite(data2, frame->width/2 * frame->height/2, 1, outfile);

						if (fi >= hit)
							fflush(outfile);

						if (fi == hit + 60)
							return -2;

					}break;
					default:
					{
						printf("default format:%d\n", pCodecCtx->pix_fmt);
						return -1;
					}
				}


			}
			av_packet_unref(&packet);

			/*
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

			switch (pCodecCtx->pix_fmt)
			{
			case AV_PIX_FMT_YUV422P:
			{
				int index = 0;
				int y_i = 0, u_i = 0, v_i = 0;
				for (index = 0; index < frame->width*frame->height * 2;)
				{
					//outputframe[index++] = frame->data[0][y_i++];
					//outputframe[index++] = frame->data[1][u_i++];
					//outputframe[index++] = frame->data[0][y_i++];
					//outputframe[index++] = frame->data[2][v_i++];
				}
			}break;
			default:
			{
				printf("default format:%d\n", pCodecCtx->pix_fmt);
				return -1;
			}
			}*/
			/*
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
			*/
		};

		if (packet.data != NULL)
			av_free_packet(&packet);
		avcodec_close(pCodecCtx);
		avformat_close_input(&pFormatCtx);
		fclose(outfile);
		return 0;
	}

	static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
		char *filename)
	{
		FILE *f;
		int i;

		f = fopen(filename, "w");
		fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
		for (i = 0; i < ysize; i++)
			fwrite(buf + i * wrap, 1, xsize, f);
		fclose(f);
	}

	static void decodeVideo(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
		const char *filename)
	{
		char buf[1024];
		int ret;

		ret = avcodec_send_packet(dec_ctx, pkt);
		if (ret < 0) {
			fprintf(stderr, "Error sending a packet for decoding\n");
			exit(1);
		}

		while (ret >= 0) {
			ret = avcodec_receive_frame(dec_ctx, frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				return;
			else if (ret < 0) {
				fprintf(stderr, "Error during decoding\n");
				exit(1);
			}

			printf("saving frame %3d\n", dec_ctx->frame_number);
			fflush(stdout);

			/* the picture is allocated by the decoder. no need to
			free it */
			snprintf(buf, sizeof(buf), "%s-%d", filename, dec_ctx->frame_number);
			pgm_save(frame->data[0], frame->linesize[0],
				frame->width, frame->height, buf);
		}
	}


	int Pioneer::testDecodeVideo(int argc, const char* argv[])
	{
		const char *filename, *outfilename;
		const AVCodec *codec;
		AVCodecParserContext *parser;
		AVCodecContext *c = NULL;
		FILE *f;
		AVFrame *frame;
#define INBUF_SIZE 4096
		uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
		uint8_t *data;
		size_t   data_size;
		int ret;
		AVPacket *pkt;

		//if (argc <= 2) {
		//	fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
		//	exit(0);
		//}
		filename = "calnifornia.mkv";// "centaur_1.mpg";// argv[1];
		outfilename = "california.out";// argv[2];

		pkt = av_packet_alloc();
		if (!pkt)
			exit(1);

		/* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
		memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

		/* find the MPEG-1 video decoder */
		codec = avcodec_find_decoder(AV_CODEC_ID_MPEG1VIDEO);
		if (!codec) {
			fprintf(stderr, "Codec not found\n");
			exit(1);
		}

		parser = av_parser_init(codec->id);
		if (!parser) {
			fprintf(stderr, "parser not found\n");
			exit(1);
		}

		c = avcodec_alloc_context3(codec);
		if (!c) {
			fprintf(stderr, "Could not allocate video codec context\n");
			exit(1);
		}

		/* For some codecs, such as msmpeg4 and mpeg4, width and height
		MUST be initialized there because this information is not
		available in the bitstream. */

		/* open it */
		if (avcodec_open2(c, codec, NULL) < 0) {
			fprintf(stderr, "Could not open codec\n");
			exit(1);
		}

		f = fopen(filename, "rb");
		if (!f) {
			fprintf(stderr, "Could not open %s\n", filename);
			exit(1);
		}

		frame = av_frame_alloc();
		if (!frame) {
			fprintf(stderr, "Could not allocate video frame\n");
			exit(1);
		}

		while (!feof(f)) {
			/* read raw data from the input file */
			data_size = fread(inbuf, 1, INBUF_SIZE, f);
			if (!data_size)
				break;

			/* use the parser to split the data into frames */
			data = inbuf;
			while (data_size > 0) {
				ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size,
					data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
				if (ret < 0) {
					fprintf(stderr, "Error while parsing\n");
					exit(1);
				}
				data += ret;
				data_size -= ret;

				if (pkt->size)
					decodeVideo(c, frame, pkt, outfilename);
			}
		}

		/* flush the decoder */
		decodeVideo(c, frame, NULL, outfilename);

		fclose(f);

		av_parser_close(parser);
		avcodec_free_context(&c);
		av_frame_free(&frame);
		av_packet_free(&pkt);

		return 0;
	}

	int Pioneer::testPioneer(int argc, const char* argv[])
	{
		//return testMediaInfo(argc, argv);
		//return testDecodeMp2(argc, argv);
		//return testDecodeAudio(argc, argv);
		//return testDecodeVideo(argc, argv);
		return testDecodeVideo2(argc, argv);
	}
}
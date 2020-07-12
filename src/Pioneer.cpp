#include "Pioneer.h"
#include <stdio.h>
#include <string>
#include <iostream>
#include <vector>
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

		const char* infile_name = "test.mov";// "california.mkv";// "mojito.mp3";

		AVFormatContext *pFormatCtx = NULL;
		int ret = avformat_open_input(&pFormatCtx, infile_name, NULL, NULL);
		if (ret < 0)
		{
			printf("avformat_open_input open %s failed\n", infile_name);
			return -2;
		}

		ret = avformat_find_stream_info(pFormatCtx, NULL);
		if (ret < 0)
		{
			avformat_close_input(&pFormatCtx);
			printf("Couldn't find stream information in file %s\n", infile_name);
			return -3;
		}
		av_dump_format(pFormatCtx, 0, infile_name, false);

		int audioStreamID = -1;
		int videoStreamID = -1;
		for (int i = 0; i<pFormatCtx->nb_streams; i++)
		{
			if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				videoStreamID = i;
			}
			if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				audioStreamID = i;
			}
		}
		if (videoStreamID == -1 || audioStreamID == -1)
		{
			avformat_close_input(&pFormatCtx);
			printf("Didn't find a audio stream in file %s\n", infile_name);
			return -4;
		}

		//avcodec_alloc_context3()
		//avcodec_parameters_to_context()
		//sws_getContext();
		AVCodecContext* pCodecCtx = pFormatCtx->streams[videoStreamID]->codec;

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


		SDL_Window * screen = NULL;
		SDL_Renderer* sdlRenderer = NULL;
		SDL_Texture * sdlTexture = NULL;
		SDL_Rect sdlRect;
		SDL_Event event;
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) 
		{
			printf("Could not initialize SDL - %s\n", SDL_GetError()); 		
			return -1;
		}
		int screen_w = pCodecCtx->width;
		int screen_h = pCodecCtx->height;
		screen = SDL_CreateWindow("myT1", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_SHOWN);
		if(!screen)	
		{
			fprintf(stderr,"Could not create window: %s\n", SDL_GetError());
			return -1;
		} 
		sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
		SDL_RenderClear(sdlRenderer);
		sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height); 
		sdlRect.x = 0;
		sdlRect.y = 0;
		sdlRect.w = screen_w;
		sdlRect.h = screen_h;




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
			} while (packet.stream_index != videoStreamID);
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
						/*
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
						*/

						int res = SDL_UpdateYUVTexture(sdlTexture, &sdlRect, 
							frame->data[0], frame->linesize[0], 
							frame->data[1], frame->linesize[1],
							frame->data[2], frame->linesize[2]);
						
						res = SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
						SDL_RenderPresent(sdlRenderer);
						SDL_Delay(20);  


						SDL_PollEvent(&event);
						switch (event.type)
						{
						case SDL_QUIT:
							SDL_Quit();
							//exit(0);
							break;
						default:
							break;
						};



						
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

		SDL_Quit();
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

	///////////////////////////////////////////////////////////////////////////////////////

	void saveFrame(AVFrame *avFrame, int width, int height, int frameIndex)
	{
		char szFilename[32];
		sprintf(szFilename, "frame%d.ppm", frameIndex);
		FILE* pFile = fopen(szFilename, "wb");
		if (pFile == NULL)
			return;
		fprintf(pFile, "P6\n%d %d\n255\n", width, height);
		for (int y = 0; y < height; y++)
		{
			fwrite(avFrame->data[0] + y * avFrame->linesize[0], 1, width * 3, pFile);
		}
		fclose(pFile);
	}





#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

	int audio_decode_frame(AVCodecContext * aCodecCtx, uint8_t * audio_buf, int buf_size);

	typedef struct PacketQueue
	{
		AVPacketList* first_pkt;
		AVPacketList* last_pkt;
		int nb_packets;
		int size;
		SDL_mutex* mutex;
		SDL_cond* cond;
	} PacketQueue;

	PacketQueue audioq;

	bool packet_queue_init(PacketQueue * q)
	{
		memset(q, 0, sizeof(PacketQueue));
		q->mutex = SDL_CreateMutex();
		if (q->mutex == NULL)
			return false;
		q->cond = SDL_CreateCond();
			return false;
	}

	int quit = 0;
	/**
	* Pull in data from audio_decode_frame(), store the result in an intermediary
	* buffer, attempt to write as many bytes as the amount defined by len to
	* stream, and get more data if we don't have enough yet, or save it for later
	* if we have some left over.
	*
	* @param   userdata    the pointer we gave to SDL.
	* @param   stream      the buffer we will be writing audio data to.
	* @param   len         the size of that buffer.
	*/
	void audio_callback(void * userdata, Uint8 * stream, int len)
	{
		AVCodecContext * aCodecCtx = (AVCodecContext *)userdata;

		int len1 = -1;
		int audio_size = -1;

		static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
		static unsigned int audio_buf_size = 0;
		static unsigned int audio_buf_index = 0;

		while (len > 0)
		{
			if (quit)
			{
				return;
			}

			if (audio_buf_index >= audio_buf_size)
			{
				// we have already sent all avaialble data; get more
				audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));

				// if error
				if (audio_size < 0)
				{
					// output silence
					audio_buf_size = 1024;

					// clear memory
					memset(audio_buf, 0, audio_buf_size);
					printf("audio_decode_frame() failed.\n");
				}
				else
				{
					audio_buf_size = audio_size;
				}

				audio_buf_index = 0;
			}

			len1 = audio_buf_size - audio_buf_index;

			if (len1 > len)
			{
				len1 = len;
			}

			// copy data from audio buffer to the SDL stream
			memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);

			len -= len1;
			stream += len1;
			audio_buf_index += len1;
		}
	}

	/**
	* Resample the audio data retrieved using FFmpeg before playing it.
	*
	* @param   audio_decode_ctx    the audio codec context retrieved from the original AVFormatContext.
	* @param   decoded_audio_frame the decoded audio frame.
	* @param   out_sample_fmt      audio output sample format (e.g. AV_SAMPLE_FMT_S16).
	* @param   out_channels        audio output channels, retrieved from the original audio codec context.
	* @param   out_sample_rate     audio output sample rate, retrieved from the original audio codec context.
	* @param   out_buf             audio output buffer.
	*
	* @return                      the size of the resampled audio data.
	*/
	static int audio_resampling(                                  // 1
		AVCodecContext * audio_decode_ctx,
		AVFrame * decoded_audio_frame,
		enum AVSampleFormat out_sample_fmt,
		int out_channels,
		int out_sample_rate,
		uint8_t * out_buf
	)
	{
		// check global quit flag
		if (quit)
		{
			return -1;
		}

		SwrContext * swr_ctx = NULL;
		int ret = 0;
		int64_t in_channel_layout = audio_decode_ctx->channel_layout;
		int64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
		int out_nb_channels = 0;
		int out_linesize = 0;
		int in_nb_samples = 0;
		int out_nb_samples = 0;
		int max_out_nb_samples = 0;
		uint8_t ** resampled_data = NULL;
		int resampled_data_size = 0;

		swr_ctx = swr_alloc();

		if (!swr_ctx)
		{
			printf("swr_alloc error.\n");
			return -1;
		}

		// get input audio channels
		in_channel_layout = (audio_decode_ctx->channels ==
			av_get_channel_layout_nb_channels(audio_decode_ctx->channel_layout)) ?   // 2
			audio_decode_ctx->channel_layout :
			av_get_default_channel_layout(audio_decode_ctx->channels);

		// check input audio channels correctly retrieved
		if (in_channel_layout <= 0)
		{
			printf("in_channel_layout error.\n");
			return -1;
		}

		// set output audio channels based on the input audio channels
		if (out_channels == 1)
		{
			out_channel_layout = AV_CH_LAYOUT_MONO;
		}
		else if (out_channels == 2)
		{
			out_channel_layout = AV_CH_LAYOUT_STEREO;
		}
		else
		{
			out_channel_layout = AV_CH_LAYOUT_SURROUND;
		}

		// retrieve number of audio samples (per channel)
		in_nb_samples = decoded_audio_frame->nb_samples;
		if (in_nb_samples <= 0)
		{
			printf("in_nb_samples error.\n");
			return -1;
		}

		// Set SwrContext parameters for resampling
		av_opt_set_int(   // 3
			swr_ctx,
			"in_channel_layout",
			in_channel_layout,
			0
		);

		// Set SwrContext parameters for resampling
		av_opt_set_int(
			swr_ctx,
			"in_sample_rate",
			audio_decode_ctx->sample_rate,
			0
		);

		// Set SwrContext parameters for resampling
		av_opt_set_sample_fmt(
			swr_ctx,
			"in_sample_fmt",
			audio_decode_ctx->sample_fmt,
			0
		);

		// Set SwrContext parameters for resampling
		av_opt_set_int(
			swr_ctx,
			"out_channel_layout",
			out_channel_layout,
			0
		);

		// Set SwrContext parameters for resampling
		av_opt_set_int(
			swr_ctx,
			"out_sample_rate",
			out_sample_rate,
			0
		);

		// Set SwrContext parameters for resampling
		av_opt_set_sample_fmt(
			swr_ctx,
			"out_sample_fmt",
			out_sample_fmt,
			0
		);

		// Once all values have been set for the SwrContext, it must be initialized
		// with swr_init().
		ret = swr_init(swr_ctx);;
		if (ret < 0)
		{
			printf("Failed to initialize the resampling context.\n");
			return -1;
		}

		max_out_nb_samples = out_nb_samples = av_rescale_rnd(
			in_nb_samples,
			out_sample_rate,
			audio_decode_ctx->sample_rate,
			AV_ROUND_UP
		);

		// check rescaling was successful
		if (max_out_nb_samples <= 0)
		{
			printf("av_rescale_rnd error.\n");
			return -1;
		}

		// get number of output audio channels
		out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);

		ret = av_samples_alloc_array_and_samples(
			&resampled_data,
			&out_linesize,
			out_nb_channels,
			out_nb_samples,
			out_sample_fmt,
			0
		);

		if (ret < 0)
		{
			printf("av_samples_alloc_array_and_samples() error: Could not allocate destination samples.\n");
			return -1;
		}

		// retrieve output samples number taking into account the progressive delay
		out_nb_samples = av_rescale_rnd(
			swr_get_delay(swr_ctx, audio_decode_ctx->sample_rate) + in_nb_samples,
			out_sample_rate,
			audio_decode_ctx->sample_rate,
			AV_ROUND_UP
		);

		// check output samples number was correctly retrieved
		if (out_nb_samples <= 0)
		{
			printf("av_rescale_rnd error\n");
			return -1;
		}

		if (out_nb_samples > max_out_nb_samples)
		{
			// free memory block and set pointer to NULL
			av_free(resampled_data[0]);

			// Allocate a samples buffer for out_nb_samples samples
			ret = av_samples_alloc(
				resampled_data,
				&out_linesize,
				out_nb_channels,
				out_nb_samples,
				out_sample_fmt,
				1
			);

			// check samples buffer correctly allocated
			if (ret < 0)
			{
				printf("av_samples_alloc failed.\n");
				return -1;
			}

			max_out_nb_samples = out_nb_samples;
		}

		if (swr_ctx)
		{
			// do the actual audio data resampling
			ret = swr_convert(
				swr_ctx,
				resampled_data,
				out_nb_samples,
				(const uint8_t **)decoded_audio_frame->data,
				decoded_audio_frame->nb_samples
			);

			// check audio conversion was successful
			if (ret < 0)
			{
				printf("swr_convert_error.\n");
				return -1;
			}

			// Get the required buffer size for the given audio parameters
			resampled_data_size = av_samples_get_buffer_size(
				&out_linesize,
				out_nb_channels,
				ret,
				out_sample_fmt,
				1
			);

			// check audio buffer size
			if (resampled_data_size < 0)
			{
				printf("av_samples_get_buffer_size error.\n");
				return -1;
			}
		}
		else
		{
			printf("swr_ctx null error.\n");
			return -1;
		}

		// copy the resampled data to the output buffer
		memcpy(out_buf, resampled_data[0], resampled_data_size);

		/*
		* Memory Cleanup.
		*/
		if (resampled_data)
		{
			// free memory block and set pointer to NULL
			av_freep(&resampled_data[0]);
		}

		av_freep(&resampled_data);
		resampled_data = NULL;

		if (swr_ctx)
		{
			// Free the given SwrContext and set the pointer to NULL
			swr_free(&swr_ctx);
		}

		return resampled_data_size;
	}

	/**
	* Put the given AVPacket in the given PacketQueue.
	*
	* @param   q   the queue to be used for the insert
	* @param   pkt the AVPacket to be inserted in the queue
	*
	* @return      0 if the AVPacket is correctly inserted in the given PacketQueue.
	*/
	int packet_queue_put(PacketQueue * q, AVPacket * pkt)
	{
		/* [4]
		* if (av_dup_packet(pkt) < 0) { return -1; }
		*/

		// alloc the new AVPacketList to be inserted in the audio PacketQueue
		AVPacketList* avPacketList;
		avPacketList = (AVPacketList*)av_malloc(sizeof(AVPacketList));

		// check the AVPacketList was allocated
		if (!avPacketList)
		{
			return -1;
		}

		// add reference to the given AVPacket
		avPacketList->pkt = *pkt;

		// the new AVPacketList will be inserted at the end of the queue
		avPacketList->next = NULL;

		// lock mutex
		SDL_LockMutex(q->mutex);

		// check the queue is empty
		if (q->last_pkt == NULL)
		{
			// if it is, insert as first
			q->first_pkt = avPacketList;
		}
		else
		{
			// if not, insert as last
			q->last_pkt->next = avPacketList;
		}

		// point the last AVPacketList in the queue to the newly created AVPacketList
		q->last_pkt = avPacketList;

		// increase by 1 the number of AVPackets in the queue
		q->nb_packets++;

		// increase queue size by adding the size of the newly inserted AVPacket
		q->size += avPacketList->pkt.size;

		// notify packet_queue_get which is waiting that a new packet is available
		SDL_CondSignal(q->cond);

		// unlock mutex
		SDL_UnlockMutex(q->mutex);

		return 0;
	}

	/**
	* Get the first AVPacket from the given PacketQueue.
	*
	* @param   q       the PacketQueue to extract from
	* @param   pkt     the first AVPacket extracted from the queue
	* @param   block   0 to avoid waiting for an AVPacket to be inserted in the given
	*                  queue, != 0 otherwise.
	*
	* @return          < 0 if returning because the quit flag is set, 0 if the queue
	*                  is empty, 1 if it is not empty and a packet was extract (pkt)
	*/
	static int packet_queue_get(PacketQueue * q, AVPacket * pkt, int block)
	{
		int ret;

		AVPacketList * avPacketList;

		// lock mutex
		SDL_LockMutex(q->mutex);

		for (;;)
		{
			// check quit flag
			if (quit)
			{
				ret = -1;
				break;
			}

			// point to the first AVPacketList in the queue
			avPacketList = q->first_pkt;

			// if the first packet is not NULL, the queue is not empty
			if (avPacketList != NULL)
			{
				// place the second packet in the queue at first position
				q->first_pkt = avPacketList->next;

				// check if queue is empty after removal
				if (q->first_pkt == NULL)
				{
					// first_pkt = last_pkt = NULL = empty queue
					q->last_pkt = NULL;
				}

				// decrease the number of packets in the queue
				q->nb_packets--;

				// decrease the size of the packets in the queue
				q->size -= avPacketList->pkt.size;

				// point pkt to the extracted packet, this will return to the calling function
				*pkt = avPacketList->pkt;

				// free memory
				av_free(avPacketList);

				ret = 1;
				break;
			}
			else if (!block)
			{
				ret = 0;
				break;
			}
			else
			{
				// unlock mutex and wait for cond signal, then lock mutex again
				SDL_CondWait(q->cond, q->mutex);
			}
		}

		// unlock mutex
		SDL_UnlockMutex(q->mutex);

		return ret;
	}

	/**
	* Get a packet from the queue if available. Decode the extracted packet. Once
	* we have the frame, resample it and simply copy it to our audio buffer, making
	* sure the data_size is smaller than our audio buffer.
	*
	* @param   aCodecCtx   the audio AVCodecContext used for decoding
	* @param   audio_buf   the audio buffer to write into
	* @param   buf_size    the size of the audio buffer, 1.5 larger than the one
	*                      provided by FFmpeg
	*
	* @return              0 if everything goes well, -1 in case of error or quit
	*/
	int audio_decode_frame(AVCodecContext * aCodecCtx, uint8_t * audio_buf, int buf_size)
	{
		AVPacket * avPacket = av_packet_alloc();
		static uint8_t * audio_pkt_data = NULL;
		static int audio_pkt_size = 0;

		// allocate a new frame, used to decode audio packets
		static AVFrame * avFrame = NULL;
		avFrame = av_frame_alloc();
		if (!avFrame)
		{
			printf("Could not allocate AVFrame.\n");
			return -1;
		}

		int len1 = 0;
		int data_size = 0;

		for (;;)
		{
			// check global quit flag
			if (quit)
			{
				return -1;
			}

			//audio_pkt_size初始化为0,所以初始不进入,之后每次从queue中取一个packet出来
			//data和size指向packet的data和长度
			while (audio_pkt_size > 0)//paket里头还有剩余数据,需要receive(decode)
			{//在这个循环内，audio_pkt_size只会一次减少到0，没有其他可能
				int got_frame = 0;

				int ret = avcodec_receive_frame(aCodecCtx, avFrame);
				if (ret == 0)
				{//decode到一个frame
					got_frame = 1;
				}
				if (ret == AVERROR(EAGAIN))
				{//decode不足产生一个frame,需要继续send数据
					ret = 0;//触发下面send数据
				}
				if (ret == 0)
				{//decode不足产生一个frame,需要继续send数据
					ret = avcodec_send_packet(aCodecCtx, avPacket);
				}

				if (ret == AVERROR(EAGAIN))
				{//send得不够数据, 需要再多给
					ret = 0;
				}
				else if (ret < 0)
				{
					printf("avcodec_receive_frame error");
					return -1;
				}
				else
				{//返回0，avPacket的size被消耗掉
					len1 = avPacket->size;
				}

				//len1为send消耗了pkt的size长度
				if (len1 < 0)
				{
					// if error, skip frame
					audio_pkt_size = 0;
					break;
				}

				audio_pkt_data += len1;//这次send消耗的packet数据的长度
				audio_pkt_size -= len1;
				data_size = 0;

				if (got_frame)
				{
					// audio resampling
					data_size = audio_resampling(
						aCodecCtx,
						avFrame,
						AV_SAMPLE_FMT_S16,
						aCodecCtx->channels,
						aCodecCtx->sample_rate,
						audio_buf
					);

					if (data_size <= buf_size)
					{

					}
					else
					{
						return -1;
					}
				}

				if (data_size <= 0)
				{
					// no data yet, get more frames
					continue;
				}

				// we have the data, return it and come back for more later
				return data_size;
			}//end while

			if (avPacket->data)
			{//清理上次为packet分配的data内存
				// wipe the packet
				av_packet_unref(avPacket);
			}

			// get more audio AVPacket
			int ret = packet_queue_get(&audioq, avPacket, 1);//block calling wait unti new packet available

			// if packet_queue_get returns < 0, the global quit flag was set
			if (ret < 0)
			{
				return -1;
			}

			audio_pkt_data = avPacket->data;
			audio_pkt_size = avPacket->size;
		}
		return 0;
	}

	int Pioneer::testDecodeVideo3(int argc, const char* argv[])
	{
		const char* filepath = "california.mkv";
		filepath = "test.mov";

		AVFormatContext* pFormatCtx = NULL;
		if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0)
			return -1;

		if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
			return -2;

		av_dump_format(pFormatCtx, 0, filepath, NULL);

		int videoStreamId = -1;
		int audioStreamId = -1;
		for (int i = 0; i < pFormatCtx->nb_streams; i++)
		{
			if (videoStreamId == -1 && pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
				videoStreamId = i;
			if (audioStreamId == -1 && pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
				audioStreamId = i;
		}
		if (videoStreamId < 0 || audioStreamId < 0)
			return -3;

		//audio
		AVCodec* aCodec = avcodec_find_decoder(pFormatCtx->streams[audioStreamId]->codecpar->codec_id);
		if (aCodec == NULL)
			return -4;
		AVCodecContext* aCodecCtx = avcodec_alloc_context3(aCodec);
		if (aCodecCtx == NULL)
			return -5;
		if (avcodec_parameters_to_context(aCodecCtx, pFormatCtx->streams[audioStreamId]->codecpar) < 0)
			return -6;
		if (avcodec_open2(aCodecCtx, aCodec, NULL) != 0)
			return -7;

		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
			return -1;
		SDL_AudioSpec wanted_specs;
		SDL_zero(wanted_specs);
		wanted_specs.freq = aCodecCtx->sample_rate;
		wanted_specs.format = AUDIO_S16SYS;
		wanted_specs.channels = aCodecCtx->channels;
		wanted_specs.silence = 0;
		wanted_specs.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_specs.callback = audio_callback;
		wanted_specs.userdata = aCodecCtx;
		SDL_AudioSpec specs;
		SDL_zero(specs);
		SDL_AudioDeviceID audioDeviceID = SDL_OpenAudioDevice(NULL, 0, &wanted_specs, &specs, 0);
		if (audioDeviceID == 0)
		{
			const char* error = SDL_GetError();
			printf("%s\n", error);
			return -7;
		}
		packet_queue_init(&audioq);
		SDL_PauseAudioDevice(audioDeviceID, 0);


		//video
		AVCodec* pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStreamId]->codecpar->codec_id);
		if (pCodec == NULL)
			return -4;
		AVCodecContext* pCodecCtx = avcodec_alloc_context3(pCodec);
		if (pCodecCtx == NULL)
			return -5;
		if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStreamId]->codecpar) < 0)
			return -6;
		if (avcodec_open2(pCodecCtx, pCodec, NULL) != 0)
			return -7;

		AVFrame* pFrame = av_frame_alloc();
		if (pFrame == NULL)
			return -8;
		AVFrame* pFrameRgb = av_frame_alloc();
		if (pFrameRgb == NULL)
			return -9;
		
		int buflen = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 32);
		uint8_t* buffer = (uint8_t*)av_malloc(buflen);
		av_image_fill_arrays(pFrameRgb->data, pFrameRgb->linesize, buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 32);

		struct SwsContext* sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
		if (sws_ctx == NULL)
			return -10;

		SDL_Window* screen = SDL_CreateWindow("SDL Video Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, pCodecCtx->width / 2, pCodecCtx->height / 2, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
		if (screen == NULL)
			return -2;
		SDL_GL_SetSwapInterval(1);
		SDL_Renderer* renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
		if (renderer == NULL)
			return -3;
		SDL_Texture * texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
		if (texture == NULL)
			return -4;

		int maxFramesToDecode = 500;
		AVPacket* pPacket = av_packet_alloc();
		int frameOffset = 0;
		int i = 0;
		while (av_read_frame(pFormatCtx, pPacket) >= 0)
		{
			printf("[%d] %d\n", i++, pPacket->stream_index);
			if (pPacket->stream_index == videoStreamId)
			{
				if (avcodec_send_packet(pCodecCtx, pPacket) < 0)
					return -11;
				int ret = avcodec_receive_frame(pCodecCtx, pFrame);
				if (ret == 0)
				{
					sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRgb->data, pFrameRgb->linesize);

					double fps = av_q2d(pFormatCtx->streams[videoStreamId]->r_frame_rate);
					double sleep_time = 1.0 / fps;
					SDL_Delay((1000 * sleep_time) - 10);
					SDL_Rect rect;
					rect.x = 0;
					rect.y = 0;
					rect.w = pCodecCtx->width;
					rect.h = pCodecCtx->height;
					SDL_UpdateYUVTexture(texture, &rect, pFrameRgb->data[0], pFrameRgb->linesize[0], pFrameRgb->data[1], pFrameRgb->linesize[1], pFrameRgb->data[2], pFrameRgb->linesize[2]);
					SDL_RenderClear(renderer);
					SDL_RenderCopy(renderer, texture, NULL, NULL);
					SDL_RenderPresent(renderer);
					
					//saveFrame(pFrameRgb, pCodecCtx->width, pCodecCtx->height, frameOffset);

					printf("Frame[%d] %c (%d) pts %d dts %d key_frame %d [coded_picture_number %d, display_picture_number %d, %dx%d]\n",
						frameOffset,
						av_get_picture_type_char(pFrame->pict_type),
						pFrameRgb->pts,
						pFrameRgb->pkt_dts,
						pFrameRgb->key_frame,
						pFrameRgb->coded_picture_number,
						pFrameRgb->display_picture_number,
						pCodecCtx->width,
						pCodecCtx->height);

					if (++frameOffset >= maxFramesToDecode)
						break;
				}
				else if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
					return -12;
			}
			else if (pPacket->stream_index == audioStreamId)
			{
				packet_queue_put(&audioq, pPacket);
			}
			else
			{
				av_packet_unref(pPacket);
			}
			SDL_Event event;
			SDL_PollEvent(&event);
			if (event.type == SDL_QUIT)
				break;
		}
		av_packet_unref(pPacket);

		SDL_DestroyTexture(texture);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(screen);
		SDL_Quit();


		avcodec_free_context(&aCodecCtx);

		sws_freeContext(sws_ctx);
		av_free(buffer);
		av_packet_free(&pPacket);
		av_frame_free(&pFrame);
		av_free(pFrame);
		av_frame_free(&pFrameRgb);
		av_free(pFrameRgb);
		avcodec_free_context(&pCodecCtx);
		avformat_close_input(&pFormatCtx);
		return 0;
	}

	int testAudioDevice(int agrc, const char* argv[])
	{
		struct Callback
		{
			static void callback(void *userdata, Uint8 * stream, int len)
			{
				short* buffer = (short*)stream;
				int buflen = len / sizeof(short);
				static FILE* ff = NULL;
				if (ff == NULL)
					ff = fopen("mojito.pcm", "rb");
				int res = fread(buffer, sizeof(short), buflen, ff);
				printf("callback with len = %d, read = %d shorts\n", len, res);
			}
		};
		SDL_Init(SDL_INIT_AUDIO);
		SDL_AudioSpec want;
		SDL_zero(want);
		want.freq = 44100;
		want.format = AUDIO_S16SYS;
		want.channels = 2;
		want.samples = 1024;
		want.callback = Callback::callback;
		want.userdata = NULL;
		SDL_AudioSpec real;
		SDL_zero(real);
		SDL_AudioDeviceID audioDeviceID = SDL_OpenAudioDevice(NULL, 0, &want, &real, 0);
		SDL_PauseAudioDevice(audioDeviceID, 0);
		while (true)
		{
			SDL_Delay(1000);
		}
		return 0;
	}


	typedef int DecodeCallback(AVCodecContext* codecCtx, AVFrame* frame, void* userCtx);

	int Decode(AVCodecContext* codecCtx, AVPacket* packet, DecodeCallback callback, void* userCtx)
	{
		bool finish = false;
		int res = avcodec_send_packet(codecCtx, packet);
		while (res == 0 && finish == false)
		{
			AVFrame* frame = av_frame_alloc();
			if (frame == NULL)
				res = -1;
			else
			{
				res = avcodec_receive_frame(codecCtx, frame);
				if (res == 0)
				{
					if (callback != NULL)
						res = callback(codecCtx, frame, userCtx);
				}
				else if (res == AVERROR(EAGAIN))
				{
					if (packet == NULL)
						res = -2;
					else
					{
						res = 0;
						finish = true;
					}
				}
				else if (res == AVERROR_EOF)
				{
					if (packet == NULL)
					{
						res = 0;
						finish = true;
					}
					else
						res = -3;
				}
				av_frame_free(&frame);
			}
		}
		return res;
	}

	int testAudioDecode(int argc, const char* argv[])
	{
		//const char* filepath = "california.mkv";
		//const char* filepath = = "test.mov";
		const char* filepath = "mojito.mp3";

		AVFormatContext* pFormatCtx = NULL;
		if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0)
			return -1;

		if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
			return -2;

		av_dump_format(pFormatCtx, 0, filepath, NULL);

		int videoStreamId = -1;
		int audioStreamId = -1;
		for (int i = 0; i < pFormatCtx->nb_streams; i++)
		{
			if (videoStreamId == -1 && pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
				videoStreamId = i;
			if (audioStreamId == -1 && pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
				audioStreamId = i;
		}
		//if (videoStreamId < 0 || audioStreamId < 0)
		//	return -3;

		//audio
		AVCodec* aCodec = avcodec_find_decoder(pFormatCtx->streams[audioStreamId]->codecpar->codec_id);
		if (aCodec == NULL)
			return -4;
		AVCodecContext* aCodecCtx = avcodec_alloc_context3(aCodec);
		if (aCodecCtx == NULL)
			return -5;
		if (avcodec_parameters_to_context(aCodecCtx, pFormatCtx->streams[audioStreamId]->codecpar) < 0)
			return -6;
		if (avcodec_open2(aCodecCtx, aCodec, NULL) != 0)
			return -7;

		AVFrame* pFrame = av_frame_alloc();
		if (pFrame == NULL)
			return -8;

		AVPacket* pPacket = av_packet_alloc();
		if (pPacket == NULL)
			return -9;
		
		FILE* file = fopen("mojito_decode.pcm", "wb");
		if (file == NULL)
			return -10;
		int num = 0;
		bool end_of_file = false;

		struct MyDecode
		{
			static int DecodeCallback(AVCodecContext* codecCtx, AVFrame* frame, void* userCtx)
			{
				FILE* file = (FILE*)userCtx;
				int channels = codecCtx->channels;
				int sampleRate = codecCtx->sample_rate;
				AVSampleFormat sampleFmt = codecCtx->sample_fmt;
				uint64_t channelLayout = codecCtx->channel_layout;
				int nb_samples = frame->nb_samples;
				uint8_t** data = frame->data;
				int sampleBytes = av_get_bytes_per_sample(sampleFmt);
				for (int j = 0; j < nb_samples; j++)
				{
					for (int k = 0; k < channels; k++)
					{
						uint8_t* p = data[k] + sampleBytes * j;
						if (fwrite(p, sampleBytes, 1, file) != 1)
							return -3;
					}
				}
				fflush(file);
				return 0;
			}
		};

		while (true)
		{
			if (av_read_frame(pFormatCtx, pPacket) != 0)
			{
				av_packet_unref(pPacket);
				break;
			}
			printf("[%d] stream_index: %d\n", num++, pPacket->stream_index);
			if (pPacket == NULL || pPacket->stream_index == audioStreamId)
			{
				int res = Decode(aCodecCtx, pPacket, MyDecode::DecodeCallback, file);
				if (res != 0)
					return -1;
			}
			av_packet_unref(pPacket);
			SDL_Event event;
			SDL_PollEvent(&event);
			if (event.type == SDL_QUIT)
				break;
		}
		int res = Decode(aCodecCtx, NULL, MyDecode::DecodeCallback, file);


		av_packet_free(&pPacket);
		pPacket = NULL;

		fclose(file);
		return 0;
	}

	int Pioneer::testPioneer(int argc, const char* argv[])
	{
		//return testMediaInfo(argc, argv);
		//return testDecodeMp2(argc, argv);
		//return testDecodeAudio(argc, argv);
		//return testDecodeVideo(argc, argv);
		//return testDecodeVideo2(argc, argv);
		//return testDecodeVideo3(argc, argv);
		//return testAudioDevice(argc, argv);
		return testAudioDecode(argc, argv);
	}
}
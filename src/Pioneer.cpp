#include "Pioneer.h"
#include <stdio.h>
extern "C" 
{
	#include "libavformat/avformat.h"
}

namespace pioneer
{
	int Pioneer::testPioneer(int argc, const char* argv[])
	{
		AVFormatContext *fmt_ctx = NULL;
		AVDictionaryEntry *tag = NULL;
		int ret;
		/*
		if (argc != 2) 
		{
			printf("usage: %s <input_file>\n"
				"example program to demonstrate the use of the libavformat metadata API.\n"
				"\n", argv[0]);
			return 1;
		}
		*/

		const char* filename = "test.mov";

		if ((ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL)))
			return ret;
		
		
		if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
			av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
			return ret;
		}

		while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
			printf("%s=%s\n", tag->key, tag->value);

		avformat_close_input(&fmt_ctx);

		return 0;
	}
}
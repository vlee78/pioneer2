// testPioneer.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <stdio.h>
#include "Pioneer.h"
#include "Pioneer2.h"
#include "SFPlayer.h"
#include <thread>
#include <chrono>
#include <vld.h>
#include "tutorial03.h"
#include "tutorial05.h"
#include "tutorial07.h"
#include "SFReplayer.h"

int main(int argc, char* argv[])
{
	{
		char* params[] =
		{
			"tutorial.exe",
			"lyl.mp4",
			"2000"
		};
		argc = 3;
		argv = params;
		
	}
	//tutorial03::tutorial03(argc, argv);
	//tutorial05::tutorial05(argc, argv);
	//tutorial07::tutorial07(argc, argv);

	//return pioneer::Pioneer::testPioneer(argc, argv);
	//return pioneer::Pioneer2::testPioneer2(argc, argv);
	//return pioneer::Pioneer2::testSDL(argc, argv);
	//return pioneer::Pioneer2::testSDLSample(argc, argv);

	pioneer::SFPlayer player;
	player.Init("test.mov", pioneer::SFPlayer::Default);
	//player.Init("sample.mp4", pioneer::SFPlayer::Default);
	//player.Init("california.mkv", pioneer::SFPlayer::Default);
	//player.Init("nevada.mkv");
	//player.Init("idaho.mkv");
	//player.Init("mojito.mp3", pioneer::SFPlayer::NoVideo);
	//player.Init("lyl.mp4", pioneer::SFPlayer::NoVideo);
	
	//pioneer::SFReplayer replayer;
	//replayer.Init("mojito.mp3", pioneer::SFReplayer::Default);
	//replayer.Init("test.mov", pioneer::SFReplayer::Default);
	//replayer.Init("sample.mp4", pioneer::SFReplayer::Default);
	//replayer.Init("sample1.mp2", pioneer::SFReplayer::Default);
	//replayer.Init("Oklahoma.mkv", pioneer::SFReplayer::Default);
	//replayer.Init("Nevada.mkv", pioneer::SFReplayer::Default);
	//replayer.Init("lyl.mp4", pioneer::SFReplayer::Default);

	getchar();
	return 0;
}


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

int main(int argc, char* argv[])
{
	printf("hello\n");
	//tutorial03(argc, argv);
	//return pioneer::Pioneer::testPioneer(argc, argv);
	//return pioneer::Pioneer2::testPioneer2(argc, argv);
	//return pioneer::Pioneer2::testSDL(argc, argv);
	//return pioneer::Pioneer2::testSDLSample(argc, argv);

	pioneer::SFPlayer player;
	player.Init("test.mov");
	//player.Init("california.mkv");
	//player.Init("nevada.mkv");
	//player.Init("mojito.mp3");
	
	getchar();
	return 0;
}


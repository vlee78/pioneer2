// testPioneer.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"
#include <stdio.h>
#include "Pioneer.h"
#include "Pioneer2.h"
#include "SFPlayer.h"
#include <thread>
#include <chrono>
#include <vld.h>

int main(int argc, const char* argv[])
{
	printf("hello\n");
	//return pioneer::Pioneer::testPioneer(argc, argv);
	//return pioneer::Pioneer2::testPioneer2(argc, argv);
	//return pioneer::Pioneer2::testSDL(argc, argv);
	//return pioneer::Pioneer2::testSDLSample(argc, argv);

	pioneer::SFPlayer player;
	//player.Init("test.mov");
	player.Init("california.mkv");
	
	getchar();
	return 0;
}


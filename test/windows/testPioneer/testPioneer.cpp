// testPioneer.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"
#include <stdio.h>
#include "Pioneer.h"
#include "Pioneer2.h"

int main(int argc, const char* argv[])
{
	printf("hello\n");
	//return pioneer::Pioneer::testPioneer(argc, argv);
	//return pioneer::Pioneer2::testPioneer2(argc, argv);
	return pioneer::Pioneer2::testSDL(argc, argv);
	//return pioneer::Pioneer2::testSDLSample(argc, argv);
}


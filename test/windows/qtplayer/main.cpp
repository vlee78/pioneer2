#include "QtPlayer.h"
#include <QtWidgets/QApplication>
#include "SFSubtitle.h"

int main(int argc, char *argv[])
{
	/*
    QApplication a(argc, argv);
    QtPlayer w;
    w.show();
    return a.exec();
	*/

	pioneer::SFSubtitle::ProcessSubtitle("F:\\aerial.american\\Aerial.America.S03E10.Alabama.1080p.AMZN.WEB-DL.DDP2.0.H.264-RCVR.mkv");
}

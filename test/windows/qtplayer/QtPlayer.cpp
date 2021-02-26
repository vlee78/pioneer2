#include "QtPlayer.h"
#include <qboxlayout.h>

QtPlayer::QtPlayer(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

	QWidget* player = new QWidget();
	player->setStyleSheet("background-color: orange");

	QWidget* panel = new QWidget();
	panel->setStyleSheet("background-color: #bbbbbb");
	panel->setFixedHeight(50);

	_btnBackward = new QPushButton();
	_btnBackward->setText("backward");
	_btnBackward->setFixedWidth(100);
	_btnForward = new QPushButton();
	_btnForward->setText("forward");
	_btnForward->setFixedWidth(100);

	QHBoxLayout* hl = new QHBoxLayout(panel);
	hl->addWidget(_btnBackward);
	hl->addWidget(_btnForward);
	hl->addStretch();
	panel->setLayout(hl);

	QVBoxLayout* vl = new QVBoxLayout();
	vl->addWidget(player);
	vl->addWidget(panel);

	QWidget* frame = findChild<QWidget*>("centralWidget");
	frame->setLayout(vl);

	connect(_btnBackward, SIGNAL(clicked()), this, SLOT(on_btnBackwardClicked()));
	connect(_btnForward, SIGNAL(clicked()), this, SLOT(on_btnForwardClicked()));

	HWND hwnd = (HWND)player->winId();
	_player.Init("lyl.mp4", pioneer::SFPlayer::Default, hwnd);
}

QtPlayer::~QtPlayer()
{
	_player.Uninit();
}

void QtPlayer::on_btnForwardClicked()
{
	_player.Seek(5.0);
}

void QtPlayer::on_btnBackwardClicked()
{
	_player.Seek(20.0);
}

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

	_txtState = new QLabel();
	_txtState->setText("state");
	_txtTime = new QLabel();
	_txtTime->setText("time");

	_slider = new QSlider();
	_slider->setOrientation(Qt::Horizontal);
	_slider->setMinimum(0);
	_slider->setMaximum(10000);
	_slider->setSingleStep(1);
	_slider->setTracking(false);

	QHBoxLayout* hl = new QHBoxLayout(panel);
	hl->addWidget(_btnBackward);
	hl->addWidget(_btnForward);
	hl->addWidget(_slider);
	hl->addWidget(_txtState);
	hl->addWidget(_txtTime);
	hl->addStretch();
	panel->setLayout(hl);

	QVBoxLayout* vl = new QVBoxLayout();
	vl->addWidget(player);
	vl->addWidget(panel);

	QWidget* frame = findChild<QWidget*>("centralWidget");
	frame->setLayout(vl);

	connect(_btnBackward, SIGNAL(clicked()), this, SLOT(on_btnBackwardClicked()));
	connect(_btnForward, SIGNAL(clicked()), this, SLOT(on_btnForwardClicked()));
	
	connect(_slider, SIGNAL(valueChanged(int)), this, SLOT(on_sliderChanged(int)));
	connect(_slider, SIGNAL(sliderPressed()), this, SLOT(on_sliderPressed()));
	connect(_slider, SIGNAL(sliderReleased()), this, SLOT(on_sliderReleased()));


	HWND hwnd = (HWND)player->winId();
	_player.Init("lyl.mp4", pioneer::SFPlayer::Default, hwnd);

	_refreshTimer = startTimer(100);
	_sliderDragging = false;
}

QtPlayer::~QtPlayer()
{
	_player.Uninit();
	killTimer(_refreshTimer);
}

void QtPlayer::on_btnForwardClicked()
{
	_player.Seek(5.0);
}

void QtPlayer::on_btnBackwardClicked()
{
	_player.Seek(20.0);
}

void QtPlayer::timerEvent(QTimerEvent *event)
{
	if (event->timerId() == _refreshTimer)
	{
		pioneer::SFPlayer::State state = pioneer::SFPlayer::Closed;
		double seconds = 0.0;
		_player.GetState(state, seconds);
		const char* stateName = pioneer::StateName[state];
		_txtState->setText(stateName);

		int isec = (int)seconds;
		int fsec = (int)((seconds - isec) * 100);
		int hour = isec / 3600;
		int min = (isec - hour * 3600) / 60;
		int sec = isec - hour * 3600 - min * 60;
		int msec = (int)((seconds - isec) * 100);
		char line[64] = { 0 };
		sprintf(line, "%d:%02d:%02d.%02d", hour, min, sec, msec);
		_txtTime->setText(line);

		if (_sliderDragging == false)
		{
			double duration = 0.0;
			_player.GetDuration(duration);
			int slider = (int)(seconds / duration * 10000);
			_slider->blockSignals(true);
			_slider->setValue(slider);
			_slider->blockSignals(false);
		}
	}
}

void QtPlayer::on_sliderChanged(int value)
{
	double duration = 0.0;
	_player.GetDuration(duration);
	double seconds = value / (double)10000 * duration;
	_player.Seek(seconds);
}

void QtPlayer::on_sliderPressed()
{
	_sliderDragging = true;
}

void QtPlayer::on_sliderReleased()
{
	_sliderDragging = false;
}
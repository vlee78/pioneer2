#pragma once

#include <QtWidgets/QMainWindow>
#include <qpushbutton.h>
#include <qlabel.h>
#include <qslider.h>
#include "ui_QtPlayer.h"
#include "SFPlayer.h"

class QtPlayer : public QMainWindow
{
    Q_OBJECT

public:
    QtPlayer(QWidget *parent = Q_NULLPTR);
	~QtPlayer();

private:
    Ui::QtPlayerClass ui;

	pioneer::SFPlayer _player;

	QWidget* _canvas;
	QPushButton* _btnForward;
	QPushButton* _btnBackward;

	QLabel* _txtState;
	QLabel* _txtTime;
	QSlider* _slider;
	int _refreshTimer;

	bool _sliderDragging;

	bool _fullScreen;

	void SetFullScreen(bool fullScreen);

private slots:
	void on_btnForwardClicked();
	void on_btnBackwardClicked();

	void on_sliderPressed();
	void on_sliderReleased();
	void on_sliderChanged(int value);
	
	virtual void mouseDoubleClickEvent(QMouseEvent* e);

	virtual void timerEvent(QTimerEvent* e);

	virtual void keyPressEvent(QKeyEvent* e);

	virtual bool eventFilter(QObject *target, QEvent *e);
};

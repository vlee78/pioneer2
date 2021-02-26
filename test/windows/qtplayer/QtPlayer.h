#pragma once

#include <QtWidgets/QMainWindow>
#include <qpushbutton.h>
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
	QPushButton* _btnForward;
	QPushButton* _btnBackward;

private slots:
	void on_btnForwardClicked();
	void on_btnBackwardClicked();
};

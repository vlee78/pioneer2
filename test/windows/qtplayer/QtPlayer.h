#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_QtPlayer.h"

class QtPlayer : public QMainWindow
{
    Q_OBJECT

public:
    QtPlayer(QWidget *parent = Q_NULLPTR);

private:
    Ui::QtPlayerClass ui;
};

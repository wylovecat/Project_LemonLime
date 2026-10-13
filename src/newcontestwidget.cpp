/*
 * SPDX-FileCopyrightText: 2011-2018 Project Lemon, Zhipeng Jia
 * SPDX-FileCopyrightText: 2018-2019 Project LemonPlus, Dust1404
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "newcontestwidget.h"
#include "ui_newcontestwidget.h"
//
#include "base/LemonPortable.hpp"
//
#include <QFileDialog>

NewContestWidget::NewContestWidget(QWidget *parent) : QWidget(parent), ui(new Ui::NewContestWidget) {
	ui->setupUi(this);
	connect(ui->selectButton, &QToolButton::clicked, this, &NewContestWidget::selectContestPath);
	connect(ui->savingName, &QLineEdit::textChanged, this, &NewContestWidget::savingNameChanged);
	connect(ui->contestTitle, &QLineEdit::textChanged, this, &NewContestWidget::informationChanged);
	connect(ui->savingName, &QLineEdit::textChanged, this, &NewContestWidget::informationChanged);
	connect(ui->contestPath, &QLineEdit::textChanged, this, &NewContestWidget::informationChanged);
}

NewContestWidget::~NewContestWidget() { delete ui; }

auto NewContestWidget::getContestTitle() -> QString { return ui->contestTitle->text(); }

auto NewContestWidget::getSavingName() -> QString { return ui->savingName->text(); }

auto NewContestWidget::getContestPath() -> QString { return ui->contestPath->text(); }

auto NewContestWidget::checkReady() const -> bool {
	return ! ui->contestTitle->text().isEmpty() && ! ui->contestPath->text().isEmpty() &&
	       ! ui->savingName->text().isEmpty();
}

void NewContestWidget::selectContestPath() {
	QString path = QFileDialog::getExistingDirectory(this, tr("Select Contest Path"),
	                                                 Lemon::Portable::contestBrowseDirectory());

	if (! path.isEmpty())
		ui->contestPath->setText(QDir::toNativeSeparators(path));
}

void NewContestWidget::savingNameChanged() {
	QString path = Lemon::Portable::contestBrowseDirectory();
	path = QDir::toNativeSeparators(path);
	path += QDir::separator();
	path += ui->savingName->text();
	ui->contestPath->setText(path);
}

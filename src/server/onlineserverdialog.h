/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDialog>
#include <QPointer>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QPlainTextEdit;
class QComboBox;
class QCheckBox;
class QDateTimeEdit;
class QTabWidget;
class QToolButton;
class SubmissionServer;
class Contest;

class OnlineServerDialog : public QDialog {
	Q_OBJECT
  public:
	explicit OnlineServerDialog(QWidget *parent = nullptr);

	void bindContest(Contest *contest, const QString &contestDir);
	SubmissionServer *server() const { return server_; }

  private slots:
	void onStartStop();
	void onGenerateUsers();
	void onAddUser();
	void onRemoveUser();
	void onExportCsv();
	void onImportCsv();
	void onSavePlaintextList();
	void onSetStatementPdf();
	void onClearStatementPdf();
	void onApplyContestWindow();
	void refreshUsersTable();
	void refreshStatementHint();
	void refreshContestWindow();
	void appendLog(const QString &msg);

  private:
	void buildUi();
	QWidget *buildServerTab();
	QWidget *buildContestTab();
	QWidget *buildUsersTab();
	QWidget *buildLogTab();
	QString detectLocalIp() const;
	void refreshStatusStrip();

	QPointer<Contest> contest_;
	QString contestDir_;
	SubmissionServer *server_ = nullptr;

	QTabWidget *tabs_{};

	QComboBox *bindCombo_{};
	QSpinBox *portSpin_{};
	QPushButton *startStopBtn_{};
	QLabel *statusLabel_{};
	QLabel *urlLabel_{};
	QPushButton *copyUrlBtn_{};
	QPushButton *openBrowserBtn_{};

	QLabel *statementLabel_{};
	QPushButton *setStatementBtn_{};
	QPushButton *clearStatementBtn_{};

	QCheckBox *windowEnableBox_{};
	QDateTimeEdit *startEdit_{};
	QDateTimeEdit *endEdit_{};
	QPushButton *applyWindowBtn_{};
	QLabel *windowStatusLabel_{};
	QCheckBox *autoJudgeBox_{};

	// Bottom status strip (always visible)
	QLabel *stripStatusBadge_{};
	QLabel *stripStatusText_{};
	QPushButton *stripStartStopBtn_{};

	QSpinBox *genCountSpin_{};
	QLineEdit *genPrefixEdit_{};
	QPushButton *genBtn_{};
	QPushButton *exportBtn_{};
	QPushButton *importBtn_{};
	QPushButton *removeBtn_{};
	QLineEdit *addNameEdit_{};
	QLineEdit *addPwdEdit_{};
	QPushButton *addBtn_{};

	QTableWidget *usersTable_{};
	QPlainTextEdit *logView_{};

	// last batch with plaintext, kept in memory so user can export CSV
	QList<QStringList> lastGeneratedRows_;
};

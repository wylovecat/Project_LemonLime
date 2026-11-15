/*
 * SPDX-FileCopyrightText: 2021-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include "base/LemonType.hpp"
#include "base/sandboxsettings.h"
#include "core/taskresult.h"

#include <QFuture>
#include <QList>
#include <QMap>
#include <QObject>
#include <QProcessEnvironment>
#include <QPromise>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

class Settings;
class Task;
class WindowsSandboxSession;

class TaskJudger : public QObject {
	Q_OBJECT
  public:
	TaskJudger(QObject *parent = nullptr);
	// void setCheckRejudgeMode(bool);
	void setNeedRejudge(const QList<std::pair<int, int>> &);
	void setSettings(const Settings *);
	void setTask(const Task *);
	void setContestantName(QString);
	CompileState getCompileState() const;
	// const QList< std::pair<int, int> >& getNeedRejudge() const;

	// How many test cases of this task may be judged at the same time.  Set by
	// JudgingController before the task starts so that (tasks in flight) *
	// caseWorkers stays within Settings::getMaxJudgingThreads(); a case is CPU
	// bound, so oversubscribing would make running processes share the CPU and turn
	// correct solutions into spurious time limit verdicts.
	void setCaseWorkers(int);

  private:
	// bool checkRejudgeMode;

	const QString commExecGrader = "grader";
	bool interpreterFlag{};
	const Settings *settings{};
	const Task *task{};
	QString contestantName;
	QFuture<void> cancellation;
	CompileState compileState;
	QString compileMessage;
	QString sourceFile;
	QString executableFile;
	QString arguments;
	QString diffPath;
	double compilerTimeLimitRatio{};
	double compilerMemoryLimitRatio{};
	bool disableMemoryLimitCheck{};
	bool interpreterAsWatcher{};
	QProcessEnvironment environment;
	SandboxSettings sandboxSettings;
	QString runtimeExecutable;
	QProcessEnvironment runtimeEnvironment;
	QList<int> overallStatus;
	QList<QList<int>> timeUsed;
	QList<QList<qint64>> memoryUsed;
	QList<QList<int>> score;
	QList<QList<ResultState>> result;
	QList<QStringList> message;
	QList<QStringList> inputFiles;

	QList<int> testCaseScore;

	int caseWorkers{1};

	// Verdict of one test case, computed by a worker thread and only copied into the
	// result tables by the serial replay in judge().  Keeping the two apart is what
	// makes the parallel part safe: judging a case only depends on the task, the
	// contestant's files and its own working directory, whereas the tables, the score
	// accumulation, the poison rule and the signals all depend on earlier cases.
	struct CaseOutcome {
		bool mkdirFailed{};
		QString message;
		int timeUsed{-1};
		qint64 memoryUsed{-1};
		int score{};
		ResultState result{Skipped};
	};

	bool traditionalTaskPrepare();
	void assign();
	void taskSkipped(const std::pair<int, int> &);
	void makeDialogAlert(QString);
	int judge();
	void judgeSingleCase(int subtask, int index, const QString &contestantName,
	                     const std::shared_ptr<WindowsSandboxSession> &sandboxSession,
	                     QList<QList<CaseOutcome>> &outcomes);

	QTemporaryDir temporaryDir;

  public:
	TaskResult judgeIt(QPromise<TaskResult> &promise);
  signals:
	void judgingStarted(QString);
	void dialogAlert(QString);
	void singleCaseFinished(QString, int, int, int, int, int, int, qint64);
	void singleSubtaskDependenceFinished(int, int, int);
	void compileError(int, int);
};

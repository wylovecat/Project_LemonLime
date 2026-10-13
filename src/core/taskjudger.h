/*
 * SPDX-FileCopyrightText: 2021-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include "base/LemonType.hpp"
#include "core/judgingthread.h"

#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <atomic>
#include <memory>

class Contestant;
class Settings;
class Task;
class WindowsSandboxSession;

class TaskJudger : public QObject {
	Q_OBJECT
  public:
	TaskJudger(QObject *parent = nullptr);
	// void setCheckRejudgeMode(bool);
	void setNeedRejudge(const QList<std::pair<int, int>> &);
	void setSettings(Settings *);
	void setTask(Task *);
	void setTaskId(int);
	void setContestant(Contestant *);
	Contestant *getContestant() const;
	CompileState getCompileState() const;
	// const QList< std::pair<int, int> >& getNeedRejudge() const;

  private:
	// bool checkRejudgeMode;

	const QString commExecGrader = "grader";
	bool interpreterFlag{};
	Settings *settings{};
	Task *task{};
	Contestant *contestant;
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
	// stop() runs on the caller's thread while the judging thread is blocked inside
	// QThread::wait(), so this flag is written and read by different threads.
	std::atomic<bool> isJudging{true};

	// How many test cases of this task may be judged at the same time.  Set by
	// JudgingController so that (tasks in flight) * caseWorkers stays within
	// Settings::getMaxJudgingThreads(); a case is CPU bound, so oversubscribing would
	// make running processes share the CPU and turn correct solutions into spurious
	// time limit verdicts.
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

	// Every JudgingThread that is currently running, so stop() can interrupt the
	// processes instead of waiting for them to finish.  currentThreadMutex serialises
	// the walk in stop() against registering and unregistering, and a thread is always
	// unregistered before it is deleted - otherwise a stop request arriving between
	// two test cases would dereference freed memory.
	QList<JudgingThread *> runningJudgingThreads;
	QMutex currentThreadMutex;

	int taskId;
	bool traditionalTaskPrepare();
	void assign();
	void taskSkipped(const std::pair<int, int> &);
	void makeDialogAlert(QString);
	int judge();
	void judgeSingleCase(int subtask, int index, const QString &contestantName,
	                     const std::shared_ptr<WindowsSandboxSession> &sandboxSession,
	                     QList<QList<CaseOutcome>> &outcomes);
	void registerJudgingThread(JudgingThread *);
	void unregisterJudgingThread(JudgingThread *);

	QTemporaryDir temporaryDir;

  public:
	void judgeIt();
	void setCaseWorkers(int);
  public slots:
	void stop();
  signals:
	void judgingStarted(QString);
	void judgingFinished();
	void dialogAlert(QString);
	void singleCaseFinished(QString, int, int, int, int, int, int, qint64);
	void singleSubtaskDependenceFinished(int, int, int);
	void compileError(int, int);
};

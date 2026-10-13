/*
 * SPDX-FileCopyrightText: 2021-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "judgingcontroller.h"
#include "core/contestant.h"
#include "sleepinhibitor.h"

#include <QtMath>

#define LEMON_MODULE_NAME "JudgingController"

JudgingController::JudgingController(Settings *settings, QObject *parent) : QObject(parent) {
	isJudging = false;
	maxJudgingThreads = qMax(1, settings->getMaxJudgingThreads());
	maxThreads = maxJudgingThreads;
	caseWorkers = 1;
	preventSleepWhileJudging = settings->getPreventSleepWhileJudging();
}

JudgingController::~JudgingController() = default;

void JudgingController::assign() {
	if (! isJudging) {
		return;
	}
	if (queuingTasks.empty())
		return;
	QThread *thread = new QThread;
	auto *taskJudger = queuingTasks.front();
	queuingTasks.pop_front();
	// The task is not running yet, so setting its case budget here cannot race with
	// judge() reading it.
	taskJudger->setCaseWorkers(caseWorkers);
	taskJudger->moveToThread(thread);
	connect(taskJudger, &TaskJudger::judgingFinished, this, &JudgingController::taskFinished);
	runningTasks[taskJudger] = thread;
	thread->start();
	QMetaObject::invokeMethod(taskJudger, &TaskJudger::judgeIt);
}

void JudgingController::taskFinished() {
	auto *taskJudger = qobject_cast<TaskJudger *>(sender());
	if (taskJudger == nullptr) {
		return;
	}
	if (runningTasks.count(taskJudger)) {
		auto *thread = runningTasks[taskJudger];
		thread->quit();
		thread->wait();
		delete thread;
		runningTasks.remove(taskJudger);
		delete taskJudger;
	}
	assign();
	if (runningTasks.empty()) {
		isJudging = false;
		sleepInhibitor.reset();
		emit judgeFinished();
	}
}
void JudgingController::start() {
	if (queuingTasks.size() == 0) {
		emit judgeFinished();
		return;
	}
	if (preventSleepWhileJudging && ! sleepInhibitor)
		sleepInhibitor = std::make_unique<SleepInhibitor>(tr("Judging submissions"));
	isJudging = true;
	// maxJudgingThreads is the total number of test cases that may run at once, and it
	// is split between the tasks in flight and the cases inside each of them.  Rejudging
	// a single submission - the common case - puts everything into that one task, so its
	// cases run in parallel instead of one after another; a whole contest spreads the
	// same budget over several contestants.  A task can no longer be given more workers
	// once it has started, so the split is decided here, when the queue is known.
	maxThreads = qMin(maxJudgingThreads, qMax(1, int(queuingTasks.size())));
	caseWorkers = qMax(1, maxJudgingThreads / maxThreads);
	while (! queuingTasks.empty() && runningTasks.size() < maxThreads) {
		assign();
	}
}
void JudgingController::stop() {
	if (! isJudging)
		return;
	isJudging = false;
	for (auto [taskJudger, thread] : runningTasks.toStdMap()) {
		// Direct: TaskJudger lives in its own QThread, which is blocked inside
		// QThread::wait() while a test case runs.  A queued call would sit in that
		// thread's event queue until the case finished, i.e. stopping would still
		// wait for the current case's time limit.
		QMetaObject::invokeMethod(taskJudger, &TaskJudger::stop, Qt::DirectConnection);
	}
	// Tasks that never started are owned by this controller and would otherwise leak;
	// assign() will not pick them up again because isJudging is now false.
	while (! queuingTasks.isEmpty())
		delete queuingTasks.dequeue();
	// emit judgeFinished();
}
void JudgingController::addTask(TaskJudger *taskJudger) { queuingTasks.push_back(taskJudger); }

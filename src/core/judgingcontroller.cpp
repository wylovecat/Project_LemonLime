/*
 * SPDX-FileCopyrightText: 2021-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "judgingcontroller.h"
#include "base/settings.h"
#include "core/contestant.h"
#include "core/taskjudger.h"
#include "sleepinhibitor.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtMath>

JudgingController::JudgingController(const Settings *settings, QObject *parent)
    : QObject(parent), preventSleepWhileJudging(settings->getPreventSleepWhileJudging()) {
	maxJudgingThreads = qMax(1, settings->getMaxJudgingThreads());
	maxThreads = maxJudgingThreads;
	caseWorkers = 1;
	threadPool.setMaxThreadCount(maxJudgingThreads);
}

JudgingController::~JudgingController() {
	stop();
	threadPool.waitForDone();
}

void JudgingController::addTask(TaskJudger *judger, Contestant *contestant, int taskId) {
	judger->setParent(this);
	queuingTasks.enqueue({judger, contestant, taskId});
}

void JudgingController::assign() {
	while (isJudging && ! queuingTasks.isEmpty() && runningTasks.size() < maxThreads) {
		const auto task = queuingTasks.dequeue();
		// The task is not running yet, so setting its case budget here cannot race with
		// judge() reading it.
		task.judger->setCaseWorkers(caseWorkers);
		auto *watcher = new QFutureWatcher<TaskResult>(this);
		connect(watcher, &QFutureWatcher<TaskResult>::finished, this,
		        [this, watcher] { finishTask(watcher); });
		runningTasks.insert(watcher, task);
		watcher->setFuture(
		    QtConcurrent::run(&threadPool, [judger = task.judger](QPromise<TaskResult> &promise) {
			    promise.addResult(judger->judgeIt(promise));
		    }));
	}
}

void JudgingController::finishTask(QFutureWatcher<TaskResult> *watcher) {
	const auto task = runningTasks.take(watcher);
	if (watcher->isCanceled()) {
		task.contestant->setCheckJudged(task.taskId, false);
	} else {
		task.contestant->applyResult(task.taskId, watcher->result());
	}
	delete task.judger;
	watcher->deleteLater();
	emit taskJudged();
	assign();
	if (runningTasks.isEmpty() && queuingTasks.isEmpty()) {
		isJudging = false;
		sleepInhibitor.reset();
		emit judgeFinished();
	}
}

void JudgingController::start() {
	if (queuingTasks.isEmpty()) {
		emit judgeFinished();
		return;
	}
	if (preventSleepWhileJudging)
		sleepInhibitor = std::make_unique<SleepInhibitor>(tr("Judging submissions"));
	// maxJudgingThreads is the total number of test cases that may run at once, and it
	// is split between the tasks in flight and the cases inside each of them.  Rejudging
	// a single submission - the common case - puts everything into that one task, so its
	// cases run in parallel instead of one after another; a whole contest spreads the
	// same budget over several contestants.  A task can no longer be given more workers
	// once it has started, so the split is decided here, when the queue is known.
	maxThreads = qMin(maxJudgingThreads, qMax(1, int(queuingTasks.size())));
	caseWorkers = qMax(1, maxJudgingThreads / maxThreads);
	isJudging = true;
	assign();
}

void JudgingController::stop() {
	if (! isJudging)
		return;
	isJudging = false;
	// Cancelling the future is what reaches the running test cases: CaseJudger and the
	// process runners poll it, so stopping does not wait for the current case's time
	// limit.
	for (auto *watcher : runningTasks.keys()) {
		// Preserve results that finished before the stop request reached the GUI.
		if (! watcher->isFinished())
			watcher->cancel();
	}
	// Tasks that never started are owned by this controller and would otherwise leak;
	// assign() will not pick them up again because isJudging is now false.
	while (! queuingTasks.isEmpty())
		delete queuingTasks.dequeue().judger;
}

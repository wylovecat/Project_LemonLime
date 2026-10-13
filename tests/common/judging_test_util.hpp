/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Shared helpers for the judging test targets (judging-unit, judging-parallel,
// judging-stop).  Keep this header free of test-case definitions: QTest needs
// the slots on the concrete class in each target's main.cpp.
#pragma once

#include "base/LemonLog.hpp"
#include "base/compiler.h"
#include "base/settings.h"
#include "core/contest.h"
#include "core/contestant.h"
#include "core/task.h"
#include "core/testcase.h"

#include "spdlog/sinks/stdout_color_sinks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <memory>

namespace JudgingTestUtil {

// The LOG/DEBUG macros dereference an inline shared_ptr logger that is null
// until this runs; any Settings setter would crash without it.
inline void initLogger() {
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	console_sink->set_level(spdlog::level::info);
	Lemon::base::logger = std::make_shared<spdlog::logger>(spdlog::logger("test", {console_sink}));
}

// A Settings with g++ and python configured for judging.  maxJudgingThreads is
// the only knob the individual targets vary.  Fails loudly when a tool chain is
// missing, so the target reports why instead of silently judging nothing.
inline Settings *createSettings(int maxJudgingThreads) {
	auto *s = new Settings();
	s->setDefaultFullScore(100);
	s->setDefaultTimeLimit(1000);
	s->setDefaultMemoryLimit(512);
	s->setDefaultExtraTimeRatio(0.2);
	// Generous on purpose: a cold runner pays the one-time start-up cost of the
	// whole tool chain on the very first compile.
	s->setCompileTimeLimit(60000);
	s->setSpecialJudgeTimeLimit(10000);
	s->setFileSizeLimit(50);
	s->setRejudgeTimes(1);
	s->setMaxJudgingThreads(maxJudgingThreads);
	s->setDefaultInputFileExtension("in");
	s->setDefaultOutputFileExtension("out");
	s->setInputFileExtensions("in");
	s->setOutputFileExtensions("out;ans");
	// Deliberately no loadSettings(): it would read the system QSettings and
	// clobber the intentional configuration above.

	QString gppPath = QStandardPaths::findExecutable("g++");
	if (gppPath.isEmpty())
		qFatal("g++ not found on PATH - cannot run judging tests.");

	auto *gpp = new Compiler();
	gpp->setCompilerType(Compiler::Typical);
	gpp->setCompilerName("g++");
	gpp->setSourceExtensions("cpp;cc;cxx");
	gpp->setCompilerLocation(gppPath);
	gpp->setTimeLimitRatio(1.0);
	gpp->setMemoryLimitRatio(1.0);
	gpp->setDisableMemoryLimitCheck(false); // the memory check needs the watcher on Unix
	gpp->setInterpreterAsWatcher(false);
	gpp->addConfiguration("default", "%s.* -o %s -O2 -lm", "");
	s->addCompiler(gpp);

	QString pythonPath = QStandardPaths::findExecutable("python3");
	if (pythonPath.isEmpty())
		pythonPath = QStandardPaths::findExecutable("python");
	if (pythonPath.isEmpty())
		qFatal("python/python3 not found on PATH - cannot run judging tests.");

	auto *py = new Compiler();
	py->setCompilerType(Compiler::InterpretiveWithoutByteCode);
	py->setCompilerName("python");
	py->setSourceExtensions("py");
	py->setInterpreterLocation(pythonPath);
	py->setTimeLimitRatio(2.0); // give python more time
	py->setMemoryLimitRatio(1.0);
	py->setDisableMemoryLimitCheck(false);
	py->setInterpreterAsWatcher(false);
	py->addConfiguration("default", "", "%s.py");
	s->addCompiler(py);

	return s;
}

inline void copyDirRecursive(const QString &src, const QString &dst) {
	const QDir srcDir(src);
	QDir().mkpath(dst);
	for (const QFileInfo &fi : srcDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
		const QString target = dst + "/" + fi.fileName();
		if (fi.isDir())
			copyDirRecursive(fi.absoluteFilePath(), target);
		else
			QFile::copy(fi.absoluteFilePath(), target);
	}
}

// Judging writes compiled binaries next to the sources, so every judging round
// must run against a private copy of the fixture instead of the checked-in data.
// Returns the path of the copy, or an empty string when the copy failed.
inline QString makeWorkCopy(QTemporaryDir &tmpDir, const QString &fixtureDir, const QString &name) {
	const QString dst = tmpDir.path() + "/" + name;
	copyDirRecursive(fixtureDir, dst);
	return dst;
}

// Judging resolves the relative data/ and source/ paths against the process CWD,
// so the contest directory becomes the current directory.
inline Contest *loadContest(const QString &contestDir, const QString &cdfName, Settings *settings,
                            QObject *parent = nullptr) {
	QFile file(contestDir + "/" + cdfName);
	if (! file.open(QFile::ReadOnly))
		return nullptr;

	QJsonParseError parseError;
	QJsonObject inObj = QJsonDocument::fromJson(file.readAll(), &parseError).object();
	if (parseError.error != QJsonParseError::NoError)
		return nullptr;

	QDir::setCurrent(contestDir);

	auto *contest = new Contest(parent);
	contest->setSettings(settings);
	if (contest->readFromJson(inObj) == -1) {
		delete contest;
		return nullptr;
	}
	return contest;
}

// The verdict matrix of tests/data/TestContest1, as asserted by test1 (which runs
// it serially).  Returns an empty string on success, otherwise a description of
// the first mismatch -- a return value rather than QVERIFY so that callers keep
// the failure inside the test slot and can name the round.
inline QString verifyTestContest1Matrix(Contest *contest) {
	const QList<Task *> &tasks = contest->getTaskList();
	if (tasks.size() != 2)
		return QString("task count: expected 2, got %1").arg(tasks.size());

	int helloworldIdx = -1;
	int aplusbIdx = -1;
	for (int i = 0; i < tasks.size(); i++) {
		if (tasks[i]->getProblemTitle() == "helloworld")
			helloworldIdx = i;
		if (tasks[i]->getProblemTitle() == "aplusb")
			aplusbIdx = i;
	}
	if (helloworldIdx < 0)
		return "Task 'helloworld' not found";
	if (aplusbIdx < 0)
		return "Task 'aplusb' not found";

	const QList<Contestant *> contestants = contest->getContestantList();
	if (contestants.size() != 2)
		return QString("contestant count: expected 2, got %1").arg(contestants.size());

	for (const Contestant *c : contestants) {
		for (int t = 0; t < tasks.size(); t++) {
			if (! c->getCheckJudged(t))
				return QString("contestant '%1' task %2 was not judged")
				    .arg(c->getContestantName())
				    .arg(t);
			// A judged task always reports one row per test case, even when the
			// submission could not be prepared at all.
			const int got = c->getResult(t).size();
			const int want = tasks.at(t)->getTestCaseList().size();
			if (got != want)
				return QString("contestant '%1' task %2: %3 result rows, expected %4")
				    .arg(c->getContestantName())
				    .arg(t)
				    .arg(got)
				    .arg(want);
		}
	}

	Contestant *user1 = contest->getContestant("user1");
	Contestant *user2 = contest->getContestant("user2");
	if (! user1)
		return "Contestant 'user1' not found";
	if (! user2)
		return "Contestant 'user2' not found";

	// RE and MLE are treated as equivalent (platform dependent).
	auto okRuntime = [](ResultState r) { return r == RunTimeError || r == MemoryLimitExceeded; };

	// user2 always prints "AC", so all 6 test cases must be AC.
	{
		const auto &res = user2->getResult(helloworldIdx);
		if (res.size() != 6)
			return QString("user2 helloworld: %1 rows, expected 6").arg(res.size());
		for (int tc = 0; tc < 6; tc++) {
			if (res[tc][0] != CorrectAnswer)
				return QString("user2 helloworld case %1: expected AC, got %2")
				    .arg(tc)
				    .arg(int(res[tc][0]));
		}
	}

	// user1: AC, TLE, MLE, WA, RE, WA
	{
		const auto &res = user1->getResult(helloworldIdx);
		if (res.size() != 6)
			return QString("user1 helloworld: %1 rows, expected 6").arg(res.size());
		if (res[0][0] != CorrectAnswer)
			return QString("user1 helloworld case 0: expected AC, got %1").arg(int(res[0][0]));
		if (res[1][0] != TimeLimitExceeded)
			return QString("user1 helloworld case 1: expected TLE, got %1").arg(int(res[1][0]));
		if (! okRuntime(res[2][0]))
			return QString("user1 helloworld case 2: expected MLE/RE, got %1").arg(int(res[2][0]));
		if (res[3][0] != WrongAnswer)
			return QString("user1 helloworld case 3: expected WA, got %1").arg(int(res[3][0]));
		if (! okRuntime(res[4][0]))
			return QString("user1 helloworld case 4: expected RE/MLE, got %1").arg(int(res[4][0]));
		if (res[5][0] != WrongAnswer)
			return QString("user1 helloworld case 5: expected WA, got %1").arg(int(res[5][0]));
	}

	// user2 has no aplusb source: the task is judged with one CompileFailed per
	// test case instead of an empty result table.
	{
		const auto &res = user2->getResult(aplusbIdx);
		if (res.size() != 1 || res.at(0).size() != 1)
			return QString("user2 aplusb shape: %1 rows, first row %2, expected 1x1")
			    .arg(res.size())
			    .arg(res.isEmpty() ? -1 : int(res.at(0).size()));
		if (res.at(0).at(0) != CompileFailed)
			return QString("user2 aplusb: expected CompileFailed, got %1").arg(int(res.at(0).at(0)));
	}

	return {};
}

} // namespace JudgingTestUtil

/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Soak target for the crash reports behind #315 ("Windows 下多线程评测容易闪退").
//
// The other judging targets prove that one round is correct.  This one judges the
// same fixture round after round, in one process, so the worker pool, the
// JudgingThread objects, the contest and its contestants are created and destroyed
// many times in a row.  A use-after-free, a pointer that outlives its task or a
// state leak across rounds shows up here as a crash, a hang or a verdict mismatch
// rather than as an occasional field report.
//
// A round is cheap (a compile plus a handful of fast test cases), so the default
// count still fits a CI timeout; LEMON_JUDGE_SOAK_ROUNDS may be raised for a long
// run locally, e.g. LEMON_JUDGE_SOAK_ROUNDS=50.

#include "../common/judging_test_util.hpp"

#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <QTimer>

namespace {

	constexpr int kDefaultRounds = 8;

	// The interruption round stops a never-terminating fixture, whose single test
	// case has a 60 s limit.  If the stop request never reached the process, the
	// round would take at least that long.
	constexpr qint64 kStopBudgetMs = 25000;

	int soakRounds() {
		bool ok = false;
		const int fromEnvironment = qEnvironmentVariableIntValue("LEMON_JUDGE_SOAK_ROUNDS", &ok);
		return ok && fromEnvironment > 0 ? fromEnvironment : kDefaultRounds;
	}

	// More concurrent CPU-bound guests than the machine has cores starves the Unix
	// watcher, which can turn a TLE into a "guest never started" report.  Capping the
	// pool at the hardware still exercises the parallel path on a 2-core CI runner
	// (2 workers) while a workstation gets the interesting 8-way load.  A deliberate
	// stress run can overcommit with LEMON_JUDGE_SOAK_THREADS; a round the host cannot
	// keep up with is skipped rather than failed.
	int soakThreads() {
		bool ok = false;
		const int fromEnvironment = qEnvironmentVariableIntValue("LEMON_JUDGE_SOAK_THREADS", &ok);
		if (ok && fromEnvironment > 0)
			return fromEnvironment;
		return qBound(2, QThread::idealThreadCount(), 8);
	}

	// A runner that could not schedule a guest at all says so with these two
	// messages: "Start runner failed" when fork/exec gave up, "Watcher time limit
	// exceeded" when the watcher itself was starved.  Neither is a judging bug -
	// the host simply had no room for one more process - so such a round is skipped
	// instead of failed.  The verdict matrix itself is pinned by test1 and
	// judging-parallel, which judge the fixture once.
	bool hostCouldNotRunAGuest(Contest *contest) {
		const QList<Task *> &tasks = contest->getTaskList();
		for (const Contestant *contestant : contest->getContestantList())
			for (int t = 0; t < tasks.size(); t++)
				for (const QStringList &messages : contestant->getMessage(t))
					for (const QString &message : messages)
						if (message.contains("Start runner failed") ||
						    message.contains("Watcher time limit exceeded"))
							return true;
		return false;
	}

} // namespace

class JudgingSoakTest : public QObject {
	Q_OBJECT

  private:
	QString m_fixtureDir;
	QString m_stopFixtureDir;
	QTemporaryDir m_tmpRoot;

	// One complete judging round.  Returns an empty string on success, or the first
	// mismatch.  `hostTooLoaded` reports a compile that hit its own time limit or a
	// guest the host could not start, both of which say nothing about the judging
	// code.
	QString judgeOneRound(Settings *settings, int round, bool *hostTooLoaded) {
		const QString contestDir = JudgingTestUtil::makeWorkCopy(m_tmpRoot, m_fixtureDir,
		                                                         QString("round%1/TestContest1").arg(round));
		if (contestDir.isEmpty())
			return QString("round %1: could not create a working copy").arg(round);

		Contest *contest = JudgingTestUtil::loadContest(contestDir, "TestContest1.cdf", settings, this);
		if (! contest)
			return QString("round %1: failed to load TestContest1.cdf").arg(round);

		contest->refreshContestantList();
		contest->judgeAll();

		*hostTooLoaded = JudgingTestUtil::compileHitTimeLimit(contest) || hostCouldNotRunAGuest(contest);

		QString message = JudgingTestUtil::verifyTestContest1Matrix(contest);
		if (! message.isEmpty())
			message = QString("round %1: %2").arg(round).arg(message);

		delete contest;
		return message;
	}

  private slots:
	void initTestCase() {
		JudgingTestUtil::initLogger();
		m_fixtureDir = QString(TEST_DATA_DIR) + "/TestContest1";
		m_stopFixtureDir = QString(TEST_DATA_DIR) + "/JudgingStop";
		QVERIFY2(QDir(m_fixtureDir).exists(),
		         qPrintable(QString("Test data dir not found: %1").arg(m_fixtureDir)));
		QVERIFY2(QDir(m_stopFixtureDir).exists(),
		         qPrintable(QString("Test data dir not found: %1").arg(m_stopFixtureDir)));
		QVERIFY(m_tmpRoot.isValid());
	}

	void testRepeatedRoundsKeepTheirVerdicts() {
		const int rounds = soakRounds();
		Settings *settings = JudgingTestUtil::createSettings(soakThreads());

		for (int round = 0; round < rounds; round++) {
			bool hostTooLoaded = false;
			const QString message = judgeOneRound(settings, round, &hostTooLoaded);

			if (hostTooLoaded)
				QSKIP("the host could not compile or start a guest under the load of this run");

			QVERIFY2(message.isEmpty(), qPrintable(message));
		}

		delete settings;
	}

	// The Windows sandbox used to force a single worker per task, so opening it turned a
	// multi-core machine into a serial judge.  Cases now overlap there too, which means
	// several AppContainer profiles prepare against one shared session; the verdicts must
	// stay identical to the unsandboxed rounds above.
	void testSandboxedRoundsKeepTheirVerdicts() {
#ifdef Q_OS_WIN
		Settings *settings = JudgingTestUtil::createSettings(soakThreads());
		for (Compiler *compiler : settings->getCompilerList()) {
			// Keep the interpreter out of the sandbox: granting a Python installation
			// costs a long one-time ACL walk that is not what this test covers.
			if (compiler->getCompilerType() != Compiler::Typical)
				continue;
			SandboxSettings sandbox;
			sandbox.enabled = true;
			sandbox.runtime = SandboxSettings::Native;
			compiler->setSandboxSettings(sandbox);
		}

		for (int round = 0; round < 2; round++) {
			bool compileTimedOut = false;
			const QString message = judgeOneRound(settings, round + 100, &compileTimedOut);

			if (compileTimedOut)
				QSKIP("the compiler hit its own time limit - the host was too loaded for this run");

			QVERIFY2(message.isEmpty(), qPrintable(message));
		}

		delete settings;
#else
		QSKIP("the Windows sandbox only exists on Windows");
#endif
	}

	// The crash reports are about judging looping for a long time, so the stop path
	// that runs in the middle of it gets the same treatment: several rounds in which
	// the round is interrupted while the judging threads are alive.
	void testInterruptedRoundsLeaveNoPendingState() {
		Settings *settings = JudgingTestUtil::createSettings(4);

		for (int round = 0; round < 3; round++) {
			const QString contestDir = JudgingTestUtil::makeWorkCopy(
			    m_tmpRoot, m_stopFixtureDir, QString("stop%1/JudgingStop").arg(round));
			QVERIFY2(! contestDir.isEmpty(), "Could not create a working copy of the JudgingStop fixture");

			Contest *contest = JudgingTestUtil::loadContest(contestDir, "JudgingStop.cdf", settings, this);
			QVERIFY2(contest != nullptr, "Failed to load JudgingStop.cdf");
			contest->refreshContestantList();

			QElapsedTimer timer;
			timer.start();

			// Long enough for the compile to be over and the infinite loop to be the
			// thing the case is running; short enough to stay far below the case's
			// own time limit.
			QTimer::singleShot(1500, contest, &Contest::stopJudgingSlot);
			contest->judgeAll();

			QVERIFY2(timer.elapsed() < kStopBudgetMs,
			         qPrintable(QString("round %1: stop was ignored; judgeAll() took %2 ms")
			                        .arg(round)
			                        .arg(timer.elapsed())));

			Contestant *user1 = contest->getContestant("user1");
			QVERIFY2(user1 != nullptr, "Contestant 'user1' not found");
			QVERIFY2(! user1->getCheckJudged(0), "An interrupted task was recorded as judged");

			delete contest;
		}

		delete settings;
	}
};

QTEST_GUILESS_MAIN(JudgingSoakTest)
#include "main.moc"

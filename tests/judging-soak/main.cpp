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

} // namespace

class JudgingSoakTest : public QObject {
	Q_OBJECT

  private:
	QString m_fixtureDir;
	QString m_stopFixtureDir;
	QTemporaryDir m_tmpRoot;

	// One complete judging round.  Returns an empty string on success, or the first
	// mismatch.  `compileTimedOut` reports a tool chain that was too slow under the
	// load this test creates, which says nothing about the judging code.
	QString judgeOneRound(Settings *settings, int round, bool *compileTimedOut) {
		const QString contestDir = JudgingTestUtil::makeWorkCopy(
		    m_tmpRoot, m_fixtureDir, QString("round%1/TestContest1").arg(round));
		if (contestDir.isEmpty())
			return QString("round %1: could not create a working copy").arg(round);

		Contest *contest = JudgingTestUtil::loadContest(contestDir, "TestContest1.cdf", settings, this);
		if (! contest)
			return QString("round %1: failed to load TestContest1.cdf").arg(round);

		contest->refreshContestantList();
		contest->judgeAll();

		*compileTimedOut = JudgingTestUtil::compileHitTimeLimit(contest);

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
		Settings *settings = JudgingTestUtil::createSettings(8);

		for (int round = 0; round < rounds; round++) {
			bool compileTimedOut = false;
			const QString message = judgeOneRound(settings, round, &compileTimedOut);

			if (compileTimedOut)
				QSKIP("the compiler hit its own time limit - the host was too loaded for this run");

			QVERIFY2(message.isEmpty(), qPrintable(message));
		}

		delete settings;
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

			Contest *contest =
			    JudgingTestUtil::loadContest(contestDir, "JudgingStop.cdf", settings, this);
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

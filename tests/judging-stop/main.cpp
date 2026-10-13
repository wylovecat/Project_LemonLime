/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Stopping a judging round must interrupt the test case that is currently
// running, not merely flip a flag that the judging loop only notices once the
// case has run to its own time limit.
//
// The fixture gives its single test case a 30000 ms time limit and a source that
// never terminates, so the two behaviours are orders of magnitude apart:
//   - stop reaches the running process  -> judgeAll() returns in a few seconds
//   - stop does not reach it            -> judgeAll() runs for ~30 s +

#include "../common/judging_test_util.hpp"

#include <QElapsedTimer>
#include <QTimer>
#include <QtTest>

// The fixture gives its single test case a 60000 ms time limit and a source that
// never terminates, and the stop is requested 5 s in.  The two behaviours are then
// an order of magnitude apart:
//   - stop is honoured -> the round ends within a second of the stop request
//   - stop is ignored  -> the round runs the full 60 s test case
// 25 s leaves a wide margin on both sides.
static const qint64 STOP_BUDGET_MS = 25000;

class JudgingStopTest : public QObject {
	Q_OBJECT

  private:
	QString m_fixtureDir;
	QTemporaryDir m_tmpRoot;

  private slots:
	void initTestCase() {
		JudgingTestUtil::initLogger();
		m_fixtureDir = QString(TEST_DATA_DIR) + "/JudgingStop";
		QVERIFY2(QDir(m_fixtureDir).exists(),
		         qPrintable(QString("Test data dir not found: %1").arg(m_fixtureDir)));
		QVERIFY(m_tmpRoot.isValid());
	}

	void testStopJudgingDoesNotWaitForTheCaseTimeLimit() {
		Settings *settings = JudgingTestUtil::createSettings(1);

		const QString contestDir = JudgingTestUtil::makeWorkCopy(m_tmpRoot, m_fixtureDir, "JudgingStop");
		QVERIFY2(! contestDir.isEmpty(), "Could not create a working copy of the fixture");

		Contest *contest = JudgingTestUtil::loadContest(contestDir, "JudgingStop.cdf", settings, this);
		QVERIFY2(contest != nullptr, "Failed to load JudgingStop.cdf");
		contest->refreshContestantList();
		QCOMPARE(contest->getContestantList().size(), 1);
		QCOMPARE(contest->getTaskList().size(), 1);

		QElapsedTimer timer;
		timer.start();

		// judgeAll() blocks in a nested event loop, so the stop request can be driven
		// from a timer inside it; stopJudgingSlot() is a public slot.
		//
		// 5 s is comfortably longer than compiling this trivial source, so by the time
		// the stop is requested the fixture's infinite loop is the thing running - the
		// round can therefore only end quickly by interrupting that process.
		QTimer::singleShot(5000, contest, &Contest::stopJudgingSlot);
		contest->judgeAll();

		const qint64 elapsed = timer.elapsed();

		QVERIFY2(elapsed < STOP_BUDGET_MS,
		         qPrintable(QString("stopJudgingSlot() was ignored: judgeAll() took %1 ms, but the test "
		                            "case time limit is 60000 ms")
		                        .arg(elapsed)));

		// Stopping cancels the task instead of recording a verdict for it:
		// TaskJudger::judgeIt() explicitly calls setCheckJudged(taskId, false) when
		// judge() reports that it did not run to completion (taskjudger.cpp:364-377).
		// What matters here is the negative: no verdict is fabricated for the case
		// that was interrupted.
		Contestant *user1 = contest->getContestant("user1");
		QVERIFY2(user1 != nullptr, "Contestant 'user1' not found");
		QVERIFY2(! user1->getCheckJudged(0),
		         "A task interrupted by stopJudgingSlot() was recorded as judged");

		delete contest;
		delete settings;
	}
};

QTEST_GUILESS_MAIN(JudgingStopTest)
#include "main.moc"

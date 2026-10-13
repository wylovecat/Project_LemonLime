/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Judging the identical fixture with concurrent task judgers must produce exactly
// the verdicts that the serial tests/test1 target asserts.  test1 stays the
// sequential oracle and is deliberately not modified.

#include "../common/judging_test_util.hpp"

#include <QtTest>

class JudgingParallelTest : public QObject {
	Q_OBJECT

  private:
	QString m_fixtureDir;
	QTemporaryDir m_tmpRoot;

	// One self-contained round: its own working copy, its own Contest, its own
	// judgeAll().  Judging writes compiled binaries next to the sources, so the
	// rounds must not share a directory (stale binaries misbehave on Windows).
	QString judgeOneRound(Settings *settings, int round) {
		const QString contestDir = JudgingTestUtil::makeWorkCopy(m_tmpRoot, m_fixtureDir,
		                                                         QString("round%1/TestContest1").arg(round));
		if (contestDir.isEmpty())
			return QString("round %1: could not create a working copy").arg(round);

		Contest *contest = JudgingTestUtil::loadContest(contestDir, "TestContest1.cdf", settings, this);
		if (! contest)
			return QString("round %1: failed to load TestContest1.cdf").arg(round);

		contest->refreshContestantList();
		contest->judgeAll();

		QString msg = JudgingTestUtil::verifyTestContest1Matrix(contest);
		if (! msg.isEmpty())
			msg = QString("round %1: %2").arg(round).arg(msg);

		delete contest;
		return msg;
	}

  private slots:
	void initTestCase() {
		JudgingTestUtil::initLogger();
		m_fixtureDir = QString(TEST_DATA_DIR) + "/TestContest1";
		QVERIFY2(QDir(m_fixtureDir).exists(),
		         qPrintable(QString("Test data dir not found: %1").arg(m_fixtureDir)));
		QVERIFY(m_tmpRoot.isValid());
	}

	// Four concurrent judgers over two contestants and two tasks.  Run twice to
	// catch state that leaks between judging rounds.
	void testParallelJudgingMatchesSerialVerdicts() {
		Settings *settings = JudgingTestUtil::createSettings(4);

		for (int round = 1; round <= 2; round++) {
			const QString msg = judgeOneRound(settings, round);
			QVERIFY2(msg.isEmpty(), qPrintable(msg));
		}

		delete settings;
	}
};

QTEST_GUILESS_MAIN(JudgingParallelTest)
#include "main.moc"

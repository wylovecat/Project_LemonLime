/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// JudgingController splits Settings::getMaxJudgingThreads() between the tasks in
// flight and the test cases inside each task.  These tests pin that split down: the
// verdict matrix of a task must not depend on how many of its cases ran at the same
// time, and independent cases must actually get faster.
//
// Both fixtures have exactly one contestant and one task, which is what puts the whole
// budget of the single task into its own cases (see JudgingController::start).

#include "../common/judging_test_util.hpp"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QtTest>

class JudgingCasesTest : public QObject {
	Q_OBJECT

  private:
	QString m_fixtureDir;
	QTemporaryDir m_tmpRoot;

	// Four cases of one task.  Each case takes about a second of CPU, and the source
	// picks its behaviour - and so its verdict, and its expected output - from the
	// input file, which also makes a mixed-up per-case working directory visible.
	static QByteArray busySource() {
		return R"CPP(#include <chrono>
#include <cstdio>

int main() {
	char word[64] = {0};
	if (std::scanf("%63s", word) != 1)
		return 0;

	auto start = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1000)) {
	}

	if (word[0] == 'R')
		return 2; // run time error
	if (word[0] == 'W') {
		std::printf("nope\n"); // wrong answer
		return 0;
	}
	std::printf("%s\n", word); // correct answer
	return 0;
}
)CPP";
	}

	static QByteArray busyCdf() {
		return R"JSON({
  "contestTitle": "JudgingCases",
  "contestants": [
    {
      "checkJudged": [false],
      "compileMesaage": [""],
      "compileState": [1],
      "contestantName": "user1",
      "inputFiles": [[]],
      "judgingTime_date": 0,
      "judgingTime_time": 0,
      "judgingTime_timespec": 0,
      "memoryUsed": [[]],
      "message": [[]],
      "result": [[]],
      "score": [[]],
      "sourceFile": [""],
      "timeUsed": [[]]
    }
  ],
  "tasks": [
    {
      "answerFileExtension": "out",
      "comparisonMode": 1,
      "compilerConfiguration": {"g++": "default", "python": "default"},
      "diffArguments": "--ignore-space-change --text --brief",
      "inputFileName": "busy.in",
      "outputFileName": "busy.out",
      "problemTitle": "busy",
      "realPrecision": 3,
      "sourceFileName": "busy",
      "specialJudge": "",
      "standardInputCheck": true,
      "standardOutputCheck": true,
      "subFolderCheck": false,
      "taskType": 0,
      "testCases": [
        {"fullScore": 25, "inputFiles": ["busy/1.in"], "memoryLimit": 512,
         "outputFiles": ["busy/1.ans"], "timeLimit": 10000},
        {"fullScore": 25, "inputFiles": ["busy/2.in"], "memoryLimit": 512,
         "outputFiles": ["busy/2.ans"], "timeLimit": 10000},
        {"fullScore": 25, "inputFiles": ["busy/3.in"], "memoryLimit": 512,
         "outputFiles": ["busy/3.ans"], "timeLimit": 10000},
        {"fullScore": 25, "inputFiles": ["busy/4.in"], "memoryLimit": 512,
         "outputFiles": ["busy/4.ans"], "timeLimit": 10000}
      ]
    }
  ]
})JSON";
	}

	static bool writeFile(const QString &path, const QByteArray &content) {
		QFile file(path);
		if (! file.open(QFile::WriteOnly))
			return false;
		return file.write(content) == content.size();
	}

	// 1 contestant, 1 task, 4 cases: AC, AC, wrong answer, run time error.
	QString buildBusyFixture(const QString &name) {
		const QString dir = m_tmpRoot.path() + "/" + name;
		QDir().mkpath(dir + "/data/busy");
		QDir().mkpath(dir + "/source/user1");

		if (! writeFile(dir + "/contest.cdf", busyCdf()))
			return {};
		if (! writeFile(dir + "/source/user1/busy.cpp", busySource()))
			return {};

		// The expected output differs per case, so a case that read another case's
		// data or wrote into another case's directory would not match.
		const char *inputs[4] = {"OK1\n", "OK2\n", "W3\n", "R4\n"};
		const char *expected[4] = {"OK1\n", "OK2\n", "OK3\n", "OK4\n"};
		for (int i = 0; i < 4; i++) {
			if (! writeFile(dir + QString("/data/busy/%1.in").arg(i + 1), inputs[i]))
				return {};
			if (! writeFile(dir + QString("/data/busy/%1.ans").arg(i + 1), expected[i]))
				return {};
		}

		return dir;
	}

	// The real fixture, reduced to one contestant so that its two tasks share the
	// case budget instead of a contestant taking it away.
	QString buildZooFixture(const QString &name) {
		const QString dir = JudgingTestUtil::makeWorkCopy(m_tmpRoot, m_fixtureDir, name + "/TestContest1");
		if (dir.isEmpty())
			return {};
		if (! QDir(dir + "/source/user2").removeRecursively())
			return {};
		return dir;
	}

	// Verdicts and scores of every case, plus whether each task was judged at all.
	// timeUsed and memoryUsed are deliberately left out: they are wall-clock
	// measurements of a process, so they differ between rounds even when nothing is
	// parallel.
	static QString matrix(Contest *contest) {
		QStringList lines;
		for (Contestant *c : contest->getContestantList()) {
			for (int t = 0; t < contest->getTaskList().size(); t++) {
				lines << QString("%1 task%2 judged=%3")
				             .arg(c->getContestantName())
				             .arg(t)
				             .arg(c->getCheckJudged(t));
				const QList<QList<ResultState>> &result = c->getResult(t);
				const QList<QList<int>> &score = c->getScore(t);
				for (int i = 0; i < result.size(); i++) {
					for (int j = 0; j < result[i].size(); j++) {
						lines << QString("  case %1/%2 verdict=%3 score=%4")
						             .arg(i)
						             .arg(j)
						             .arg(int(result[i][j]))
						             .arg(j < score[i].size() ? score[i][j] : -1);
					}
				}
			}
		}
		return lines.join("\n");
	}

	// Judge `contestDir` once with `maxJudgingThreads` and return its matrix; elapsed
	// milliseconds are reported through `elapsed`.  The two fixtures keep their own
	// file names - a work copy of TestContest1 stays TestContest1.cdf - so the caller
	// names the contest file.
	QString judgeRound(const QString &contestDir, const QString &cdfName, int maxJudgingThreads,
	                   qint64 *elapsed) {
		Settings *settings = JudgingTestUtil::createSettings(maxJudgingThreads);

		Contest *contest = JudgingTestUtil::loadContest(contestDir, cdfName, settings, this);
		if (! contest) {
			delete settings;
			return {};
		}

		contest->refreshContestantList();

		QElapsedTimer timer;
		timer.start();
		contest->judgeAll();
		*elapsed = timer.elapsed();

		const QString snapshot = matrix(contest);
		delete contest;
		delete settings;
		return snapshot;
	}

  private slots:
	void initTestCase() {
		JudgingTestUtil::initLogger();
		m_fixtureDir = QString(TEST_DATA_DIR) + "/TestContest1";
		QVERIFY2(QDir(m_fixtureDir).exists(),
		         qPrintable(QString("Test data dir not found: %1").arg(m_fixtureDir)));
		QVERIFY(m_tmpRoot.isValid());
	}

	// The verdict zoo of TestContest1 (AC, TLE, MLE/RE, WA, RE/MLE, WA) judged with
	// two cases at a time must be identical to the same fixture judged serially.
	void testParallelCasesMatchSequentialVerdicts() {
		const QString serialDir = buildZooFixture("zoo-serial");
		QVERIFY2(! serialDir.isEmpty(), "could not build the serial fixture");

		const QString parallelDir = buildZooFixture("zoo-parallel");
		QVERIFY2(! parallelDir.isEmpty(), "could not build the parallel fixture");

		qint64 serialMs = 0;
		const QString serial = judgeRound(serialDir, "TestContest1.cdf", 1, &serialMs);
		QVERIFY2(! serial.isEmpty(), "the serial round produced no matrix");

		qint64 parallelMs = 0;
		const QString parallel = judgeRound(parallelDir, "TestContest1.cdf", 4, &parallelMs);
		QVERIFY2(! parallel.isEmpty(), "the parallel round produced no matrix");

		qInfo("zoo fixture: serial %lld ms, parallel %lld ms", serialMs, parallelMs);
		QCOMPARE(parallel, serial);
	}

	void testIndependentCasesRunInParallel() {
		if (QThread::idealThreadCount() < 4)
			QSKIP("needs at least four usable cores to show a speed-up");

		const QString serialDir = buildBusyFixture("busy-serial");
		QVERIFY2(! serialDir.isEmpty(), "could not build the serial fixture");

		const QString parallelDir = buildBusyFixture("busy-parallel");
		QVERIFY2(! parallelDir.isEmpty(), "could not build the parallel fixture");

		qint64 serialMs = 0;
		const QString serial = judgeRound(serialDir, "contest.cdf", 1, &serialMs);
		QVERIFY2(! serial.isEmpty(), "the serial round produced no matrix");

		qint64 parallelMs = 0;
		const QString parallel = judgeRound(parallelDir, "contest.cdf", 4, &parallelMs);
		QVERIFY2(! parallel.isEmpty(), "the parallel round produced no matrix");

		// Same verdicts, and the per-case expected outputs still line up, so the four
		// cases did not share a working directory or an answer file.
		QCOMPARE(parallel, serial);

		qInfo("busy fixture: serial %lld ms, parallel %lld ms", serialMs, parallelMs);

		// Four one-second cases: running them one after another cannot be faster than
		// running them together.
		QVERIFY2(serialMs > 2000, qPrintable(QString("the serial round only took %1 ms, too short to "
		                                             "compare")
		                                         .arg(serialMs)));
		QVERIFY2(2 * parallelMs < serialMs,
		         qPrintable(QString("expected at least a two-fold speed-up, got %1 ms against %2 ms")
		                        .arg(parallelMs)
		                        .arg(serialMs)));
	}
};

QTEST_GUILESS_MAIN(JudgingCasesTest)
#include "main.moc"

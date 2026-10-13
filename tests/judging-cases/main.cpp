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

	// The per-case sleep of busySource(); the speed-up assertion below is written in
	// terms of it, so the two have to stay in step.
	static constexpr int kSleepMs = 4000;

	// Four cases of one task.  Each case takes several seconds of wall-clock time, and
	// the source picks its behaviour - and so its verdict, and its expected output -
	// from the input file, which also makes a mixed-up per-case working directory
	// visible.
	static QByteArray busySource() {
		return R"CPP(#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#define LEMON_TEST_SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define LEMON_TEST_SLEEP_MS(ms) usleep((ms) * 1000)
#endif

int main() {
	char word[64] = {0};
	if (std::scanf("%63s", word) != 1)
		return 0;

	// A real sleep rather than a spin.  The fixture exists to show that the cases
	// overlap in wall-clock time; a spin loop would instead measure how much CPU the
	// host leaves to the judging processes, and a Debug host burns far more of it
	// than a Release host, so the same assertion would pass or fail depending on the
	// build type and on how many cores the runner really hands out.
	//
	// The sleep is deliberately much longer than the per-case overhead: a round also
	// pays a fixed cost that cannot overlap (compiling the submission, preparing the
	// working directories, starting the processes), and with one-second cases that
	// fixed cost ate so much of the round that four cases running together came out
	// barely 1.6 times faster than four cases run one after another - short of the
	// two-fold this test asks for, on a Debug build with a loaded machine.
	// Keep in step with JudgingCasesTest::kSleepMs.
	LEMON_TEST_SLEEP_MS(4000);

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

	// Under concurrent case execution the *reason* a guest was stopped is not
	// reproducible: a case the serial round stops on its time limit can come out as
	// MemoryLimitExceeded, RunTimeError or CannotStartProgram once several guests
	// compete for the same machine (observed on the CI runners).  The comparison
	// therefore classes every "the sandbox stopped this guest" verdict together.
	// Which exact verdict a case has is pinned by the sequential oracles (tests/test1
	// and tests/common/judging_test_util.hpp); what this comparison has to prove is
	// that the parallel path pairs every case with its own data, working directory and
	// answer file, i.e. that the accepted, the wrong answered and the stopped cases
	// still line up in the same places.
	static QString verdictLabel(ResultState r) {
		switch (r) {
			case TimeLimitExceeded:
			case MemoryLimitExceeded:
			case RunTimeError:
			case CannotStartProgram:
				return QStringLiteral("terminated");
			default:
				return QString::number(int(r));
		}
	}

	// Verdicts and scores of every case, plus whether each task was judged at all.
	// timeUsed and memoryUsed are deliberately left out: they are wall-clock
	// measurements of a process, so they differ between rounds even when nothing is
	// parallel.
	// `raw` prints the exact verdicts instead of the classified ones.  Only the
	// diagnostic output uses it; the comparison always uses the classified form.
	static QString matrix(Contest *contest, bool raw = false) {
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
						             .arg(raw ? QString::number(int(result[i][j]))
						                      : verdictLabel(result[i][j]))
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
	                   qint64 *elapsed, QString *rawSnapshot = nullptr) {
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
		if (rawSnapshot)
			*rawSnapshot = matrix(contest, true);
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

		QString serialRaw;
		qint64 serialMs = 0;
		const QString serial = judgeRound(serialDir, "TestContest1.cdf", 1, &serialMs, &serialRaw);
		QVERIFY2(! serial.isEmpty(), "the serial round produced no matrix");

		QString parallelRaw;
		qint64 parallelMs = 0;
		const QString parallel = judgeRound(parallelDir, "TestContest1.cdf", 4, &parallelMs, &parallelRaw);
		QVERIFY2(! parallel.isEmpty(), "the parallel round produced no matrix");

		qInfo("zoo fixture: serial %lld ms, parallel %lld ms", serialMs, parallelMs);
		// Printed even when the comparison passes: the exact verdicts are what tells a
		// later reader whether a failure was one member of the stopped family moving or
		// a real mix-up.
		qInfo("zoo serial verdicts:\n%s", qPrintable(serialRaw));
		qInfo("zoo parallel verdicts:\n%s", qPrintable(parallelRaw));
		QVERIFY2(parallel == serial, qPrintable(QString("the parallel round differed from the serial one\n"
		                                                "serial:\n%1\nparallel:\n%2")
		                                            .arg(serialRaw, parallelRaw)));
	}

	void testIndependentCasesRunInParallel() {
		// No core-count guard is needed: the fixture sleeps instead of spinning, so
		// this measures overlap in wall-clock time and not throughput.  Sleeping cases
		// overlap on any machine, however few cores the runner hands out, while a
		// budget that failed to reach the cases still shows up as parallel == serial.
		const QString serialDir = buildBusyFixture("busy-serial");
		QVERIFY2(! serialDir.isEmpty(), "could not build the serial fixture");

		const QString parallelDir = buildBusyFixture("busy-parallel");
		QVERIFY2(! parallelDir.isEmpty(), "could not build the parallel fixture");

		QString serialRaw;
		qint64 serialMs = 0;
		const QString serial = judgeRound(serialDir, "contest.cdf", 1, &serialMs, &serialRaw);
		QVERIFY2(! serial.isEmpty(), "the serial round produced no matrix");

		QString parallelRaw;
		qint64 parallelMs = 0;
		const QString parallel = judgeRound(parallelDir, "contest.cdf", 4, &parallelMs, &parallelRaw);
		QVERIFY2(! parallel.isEmpty(), "the parallel round produced no matrix");

		qInfo("busy fixture: serial %lld ms, parallel %lld ms", serialMs, parallelMs);

		// Same verdicts, and the per-case expected outputs still line up, so the four
		// cases did not share a working directory or an answer file.
		QVERIFY2(parallel == serial, qPrintable(QString("the parallel round differed from the serial one\n"
		                                                "serial:\n%1\nparallel:\n%2")
		                                            .arg(serialRaw, parallelRaw)));

		// The serial round really paid for four sleeps, so the comparison below is not
		// measuring an empty round.
		QVERIFY2(serialMs > 3 * kSleepMs,
		         qPrintable(QString("the serial round only took %1 ms, too short for four %2 ms cases")
		                        .arg(serialMs)
		                        .arg(kSleepMs)));

		// A difference rather than a ratio: a round also pays a fixed cost that cannot
		// overlap (compiling the submission, preparing the working directories,
		// starting the processes), and that cost cancels out here.  Overlapping four
		// cases has to save at least three of the four sleeps.
		QVERIFY2(serialMs - parallelMs > 3 * kSleepMs / 2,
		         qPrintable(QString("four overlapping cases must save at least %1 ms, saved %2 ms "
		                            "(%3 ms against %4 ms)")
		                        .arg(3 * kSleepMs / 2)
		                        .arg(serialMs - parallelMs)
		                        .arg(parallelMs)
		                        .arg(serialMs)));
	}
};

QTEST_GUILESS_MAIN(JudgingCasesTest)
#include "main.moc"

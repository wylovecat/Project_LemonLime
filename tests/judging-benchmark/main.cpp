/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Throughput benchmark for case-level parallelism, with and without the Windows
// sandbox.  Skipped unless LEMON_JUDGE_BENCH=1 so CI stays fast.
//
//   LEMON_JUDGE_BENCH=1 ctest -R judging-benchmark -V
//
// Knobs: LEMON_JUDGE_BENCH_SUBTASKS (10), LEMON_JUDGE_BENCH_CASES (20),
// LEMON_JUDGE_BENCH_SLEEP_MS (40) and LEMON_JUDGE_BENCH_THREADS (ideal/2).
//
// The fixture is generated here: every case runs a compiled program that sleeps
// for a fixed time before printing the answer, so the benchmark measures judging
// throughput instead of the fixture being trivially fast.

#include "../common/judging_test_util.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace {

	int environmentInt(const char *name, int fallback) {
		bool ok = false;
		const int value = qEnvironmentVariableIntValue(name, &ok);
		return ok && value > 0 ? value : fallback;
	}

	QString sourceProgram(int sleepMilliseconds) {
		return QString(R"(
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
	int a = 0, b = 0;
	if (scanf("%d%d", &a, &b) != 2)
		return 1;
	std::this_thread::sleep_for(std::chrono::milliseconds(%1));
	printf("%d\n", a + b);
	return 0;
}
)")
		    .arg(sleepMilliseconds);
	}

	QJsonObject makeCase(int task, int index, int fullScore) {
		QJsonObject testCase;
		testCase["fullScore"] = fullScore;
		testCase["timeLimit"] = 1000;
		testCase["memoryLimit"] = 512;
		testCase["inputFiles"] = QJsonArray{QString("task%1/%2.in").arg(task).arg(index)};
		testCase["outputFiles"] = QJsonArray{QString("task%1/%2.ans").arg(task).arg(index)};
		return testCase;
	}

	QJsonObject makeTask(int task, int cases, int sleepMilliseconds) {
		QJsonArray testCases;
		const int base = 100 / cases;
		for (int index = 1; index <= cases; index++)
			testCases.append(makeCase(task, index, index == cases ? 100 - base * (cases - 1) : base));

		QJsonObject json;
		json["problemTitle"] = QString("task%1").arg(task);
		json["sourceFileName"] = QString("task%1").arg(task);
		json["inputFileName"] = QString("task%1.in").arg(task);
		json["outputFileName"] = QString("task%1.out").arg(task);
		json["standardInputCheck"] = true;
		json["standardOutputCheck"] = true;
		json["taskType"] = 0;
		json["subFolderCheck"] = false;
		json["comparisonMode"] = 1;
		json["diffArguments"] = "";
		json["realPrecision"] = 3;
		json["specialJudge"] = "";
		json["compilerConfiguration"] = QJsonObject{{"g++", "default"}};
		json["answerFileExtension"] = "out";
		json["testCases"] = testCases;
		Q_UNUSED(sleepMilliseconds);
		return json;
	}

	QJsonObject makeContestant(int subtasks) {
		QJsonArray emptyRows;
		QJsonArray emptyRow; // one empty list per task
		for (int task = 0; task < subtasks; task++)
			emptyRows.append(emptyRow);

		QJsonObject json;
		json["contestantName"] = "bench";
		json["checkJudged"] = QJsonArray::fromVariantList(QVariantList(subtasks, false));
		json["sourceFile"] = QJsonArray::fromVariantList(QVariantList(subtasks, QString()));
		json["compileMesaage"] = QJsonArray::fromVariantList(QVariantList(subtasks, QString()));
		json["inputFiles"] = emptyRows;
		json["message"] = emptyRows;
		json["score"] = emptyRows;
		json["timeUsed"] = emptyRows;
		json["memoryUsed"] = emptyRows;
		json["judgingTime_date"] = 0;
		json["judgingTime_time"] = 0;
		json["judgingTime_timespec"] = 0;
		json["compileState"] = QJsonArray::fromVariantList(QVariantList(subtasks, 1));
		json["result"] = emptyRows;
		return json;
	}

	// Writes data/, source/ and Benchmark.cdf under `root`; returns false when any
	// file could not be written.
	bool writeFixture(const QString &root, int subtasks, int cases, int sleepMilliseconds) {
		QJsonArray tasks;
		for (int task = 0; task < subtasks; task++) {
			const QString dataDirectory = root + QString("/data/task%1").arg(task);
			if (! QDir().mkpath(dataDirectory))
				return false;
			if (! QDir().mkpath(root + QString("/source/bench")))
				return false;

			QFile source(root + QString("/source/bench/task%1.cpp").arg(task));
			if (! source.open(QIODevice::WriteOnly) ||
			    source.write(sourceProgram(sleepMilliseconds).toUtf8()) < 0)
				return false;

			for (int index = 1; index <= cases; index++) {
				QFile input(dataDirectory + QString("/%1.in").arg(index));
				if (! input.open(QIODevice::WriteOnly) ||
				    input.write(QString("%1 %2\n").arg(task * 1000 + index).arg(index).toUtf8()) < 0)
					return false;

				QFile answer(dataDirectory + QString("/%1.ans").arg(index));
				if (! answer.open(QIODevice::WriteOnly) ||
				    answer.write(QString("%1\n").arg(task * 1000 + index * 2).toUtf8()) < 0)
					return false;
			}
			tasks.append(makeTask(task, cases, sleepMilliseconds));
		}

		QJsonObject cdf;
		cdf["version"] = "1.0";
		cdf["contestTitle"] = "Benchmark";
		cdf["tasks"] = tasks;
		cdf["contestants"] = QJsonArray{makeContestant(subtasks)};

		QFile file(root + "/Benchmark.cdf");
		if (! file.open(QIODevice::WriteOnly))
			return false;
		return file.write(QJsonDocument(cdf).toJson(QJsonDocument::Indented)) >= 0;
	}

	// The answer of case `index` is the sum of the two numbers in its input.
	QString verifyVerdicts(Contest *contest) {
		for (const Contestant *contestant : contest->getContestantList()) {
			for (int task = 0; task < contest->getTaskList().size(); task++) {
				const auto &results = contestant->getResult(task);
				if (results.size() != contest->getTaskList().at(task)->getTestCaseList().size())
					return QString("task %1: %2 result rows").arg(task).arg(results.size());
				for (int index = 0; index < results.size(); index++)
					if (results[index].isEmpty() || results[index][0] != CorrectAnswer)
						return QString("task %1 case %2: verdict %3")
						    .arg(task)
						    .arg(index + 1)
						    .arg(results[index].isEmpty() ? -1 : int(results[index][0]));
			}
		}
		return {};
	}

} // namespace

class JudgingBenchmarkTest : public QObject {
	Q_OBJECT

  private:
	QTemporaryDir m_tmpRoot;
	QString m_fixtureDir;
	int m_subtasks = 0;
	int m_cases = 0;
	int m_sleepMilliseconds = 0;
	int m_threads = 0;

	static void enableSandbox(Settings *settings) {
		for (Compiler *compiler : settings->getCompilerList()) {
			if (compiler->getCompilerType() != Compiler::Typical)
				continue;
			SandboxSettings sandbox;
			sandbox.enabled = true;
			sandbox.runtime = SandboxSettings::Native;
			compiler->setSandboxSettings(sandbox);
		}
	}

	// Judges one work copy and returns the wall time in milliseconds.  `message`
	// names the first problem instead of failing, so a broken phase can be reported
	// next to the timings.
	qint64 judgePhase(const QString &name, bool sandbox, int threads, QString *message) {
		const QString contestDir = JudgingTestUtil::makeWorkCopy(m_tmpRoot, m_fixtureDir, name);
		if (contestDir.isEmpty()) {
			*message = name + ": could not create a working copy";
			return -1;
		}

		Settings *settings = JudgingTestUtil::createSettings(threads);
		if (sandbox)
			enableSandbox(settings);

		Contest *contest = JudgingTestUtil::loadContest(contestDir, "Benchmark.cdf", settings, this);
		if (! contest) {
			*message = name + ": failed to load the benchmark contest";
			delete settings;
			return -1;
		}
		contest->refreshContestantList();

		QElapsedTimer timer;
		timer.start();
		contest->judgeAll();
		const qint64 elapsed = timer.elapsed();

		*message = verifyVerdicts(contest);
		if (! message->isEmpty())
			*message = name + ": " + *message;

		delete contest;
		delete settings;
		return elapsed;
	}

	void report(const QString &label, qint64 milliseconds) {
		const int total = m_subtasks * m_cases;
		qInfo().noquote() << QString("%1: %2 ms for %3 cases (%4 ms/case)")
		                         .arg(label, -26)
		                         .arg(milliseconds)
		                         .arg(total)
		                         .arg(total > 0 ? double(milliseconds) / total : 0.0, 0, 'f', 1);
	}

  private slots:
	void initTestCase() {
		JudgingTestUtil::initLogger();
		QVERIFY(m_tmpRoot.isValid());

		m_subtasks = environmentInt("LEMON_JUDGE_BENCH_SUBTASKS", 10);
		m_cases = environmentInt("LEMON_JUDGE_BENCH_CASES", 20);
		m_sleepMilliseconds = environmentInt("LEMON_JUDGE_BENCH_SLEEP_MS", 40);
		m_threads = environmentInt("LEMON_JUDGE_BENCH_THREADS", qMax(1, QThread::idealThreadCount() / 2));

		m_fixtureDir = m_tmpRoot.path() + "/fixture";
		QVERIFY2(writeFixture(m_fixtureDir, m_subtasks, m_cases, m_sleepMilliseconds),
		         "could not write the benchmark fixture");
	}

	void benchmarkParallelJudging() {
		if (! qEnvironmentVariableIsSet("LEMON_JUDGE_BENCH"))
			QSKIP("set LEMON_JUDGE_BENCH=1 to run the throughput benchmark");

		qInfo().noquote() << QString("Benchmark: %1 subtasks x %2 cases, %3 ms per case, %4 threads")
		                         .arg(m_subtasks)
		                         .arg(m_cases)
		                         .arg(m_sleepMilliseconds)
		                         .arg(m_threads);

		QString message;
		const qint64 ordinary = judgePhase("ordinary", false, m_threads, &message);
		QVERIFY2(message.isEmpty(), qPrintable(message));
		report("sandbox off, parallel", ordinary);

		qint64 sandboxed = -1;
#ifdef Q_OS_WIN
		sandboxed = judgePhase("sandbox-parallel", true, m_threads, &message);
		QVERIFY2(message.isEmpty(), qPrintable(message));
		report("sandbox on, parallel", sandboxed);

		const qint64 serial = judgePhase("sandbox-serial", true, 1, &message);
		QVERIFY2(message.isEmpty(), qPrintable(message));
		report("sandbox on, serial (old)", serial);

		if (serial > 0)
			qInfo().noquote() << QString("parallel speedup under the sandbox: %1x")
			                         .arg(double(serial) / double(sandboxed), 0, 'f', 2);
#else
		QSKIP("the Windows sandbox only exists on Windows");
#endif
	}
};

QTEST_GUILESS_MAIN(JudgingBenchmarkTest)
#include "main.moc"

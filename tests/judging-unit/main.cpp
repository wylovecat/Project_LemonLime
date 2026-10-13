/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Pure-logic regression tests: no compiler, no fixture, always runs.

#include "../common/judging_test_util.hpp"

#include "core/judgingthread.h"

#include <QJsonObject>
#include <QtTest>

class JudgingUnitTest : public QObject {
	Q_OBJECT

  private slots:
	void initTestCase() { JudgingTestUtil::initLogger(); }

	// A freshly constructed JudgingThread must already expose defined values.
	// The stop path (a comparison interrupted because judging was stopped) returns
	// from a comparator without assigning `result`, so an uninitialised member
	// would surface there as an arbitrary verdict - and enum value 0 is
	// CorrectAnswer, the worst possible one to report by accident.
	void testFreshJudgingThreadState() {
		JudgingThread thread;
		QCOMPARE(thread.getTimeUsed(), -1);
		QCOMPARE(thread.getMemoryUsed(), qint64(-1));
		QCOMPARE(thread.getJudgeTimes(), 0);
		QCOMPARE(thread.getScore(), 0);
		QCOMPARE(int(thread.getResult()), int(LastResultState));
	}

	// TestCase has no JSON field for dependenceSubtask: it is appended to the
	// inputFiles string array as "<index>_lemon_SUbtaskDEPENDENCE_fLAg".  This
	// pins that wire format, which the parallel scheduler relies on to decide
	// whether a task may run its test cases concurrently.
	void testDependenceSubtaskRoundTrip() {
		TestCase original;
		original.addSingleCase("loop/1.in", "loop/1.ans");
		original.setDependenceSubtask(QList<int>{0, 2});

		QVERIFY(original.getDependenceSubtask().contains(0));
		QVERIFY(original.getDependenceSubtask().contains(2));
		QCOMPARE(original.getInputFiles().size(), 1);

		QJsonObject obj;
		QCOMPARE(original.writeToJson(obj), 0);

		TestCase readBack;
		QCOMPARE(readBack.readFromJson(obj), 0);
		QCOMPARE(readBack.getDependenceSubtask(), original.getDependenceSubtask());
		QCOMPARE(readBack.getInputFiles().size(), 1);
		// writeToJson() stores '/' (testcase.cpp:111) and readFromJson() restores the
		// native separator (testcase.cpp:145), so compare separator-insensitively.
		QCOMPARE(QDir::fromNativeSeparators(readBack.getInputFiles().at(0)),
		         original.getInputFiles().at(0));
		QCOMPARE(readBack.getOutputFiles().size(), 1);
	}

	// Reading the same JSON twice must not accumulate the dependence entries.
	void testDependenceSubtaskDoesNotAccumulateOnRepeatedRead() {
		TestCase original;
		original.addSingleCase("loop/1.in", "loop/1.ans");
		original.setDependenceSubtask(QList<int>{1});

		QJsonObject obj;
		QCOMPARE(original.writeToJson(obj), 0);

		TestCase readBack;
		QCOMPARE(readBack.readFromJson(obj), 0);
		QCOMPARE(readBack.getDependenceSubtask().size(), 1);
		QCOMPARE(readBack.readFromJson(obj), 0);
		QCOMPARE(readBack.getDependenceSubtask().size(), 1);
	}

	// A case whose input files carry no dependence marker must report none.
	void testNoDependenceMarkerMeansNoDependency() {
		TestCase original;
		original.addSingleCase("loop/1.in", "loop/1.ans");
		original.clearDependenceSubtask();

		QJsonObject obj;
		QCOMPARE(original.writeToJson(obj), 0);

		TestCase readBack;
		QCOMPARE(readBack.readFromJson(obj), 0);
		QVERIFY(readBack.getDependenceSubtask().isEmpty());
		QCOMPARE(readBack.getInputFiles().size(), 1);
	}
};

QTEST_GUILESS_MAIN(JudgingUnitTest)
#include "main.moc"

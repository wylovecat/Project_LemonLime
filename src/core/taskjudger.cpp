/*
 * SPDX-FileCopyrightText: 2021-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "core/taskjudger.h"
#include "base/LemonLog.hpp"
#include "base/LemonType.hpp"
#include "base/compiler.h"
#include "base/settings.h"
#include "core/contestant.h"
#include "core/judgingthread.h"
#include "core/subtaskdependencelib.h"
#include "core/task.h"
#include "core/testcase.h"
#ifdef Q_OS_WIN
#include "core/windowssandbox.h"
#endif

#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>

#define LEMON_MODULE_NAME "TaskJudger"

TaskJudger::TaskJudger(QObject *parent) : QObject(parent) { compileState = NoValidSourceFile; }

void TaskJudger::setSettings(Settings *_settings) { settings = _settings; }

void TaskJudger::setTask(Task *_task) { task = _task; }

void TaskJudger::setTaskId(int id) { taskId = id; }

void TaskJudger::setContestant(Contestant *contestant) { this->contestant = contestant; }

Contestant *TaskJudger::getContestant() const { return contestant; }

// Get executable file
auto TaskJudger::traditionalTaskPrepare() -> bool {
	makeDialogAlert(tr("Preparing..."));

	// Get the source code of contestant
	compileState = NoValidSourceFile;
	QString contestantName = contestant->getContestantName();
	QDir contestantDir;
	contestantDir =
	    ! task->getSubFolderCheck()
	        ? QDir(Settings::sourcePath() + contestantName)
	        : QDir(Settings::sourcePath() + contestantName + QDir::separator() + task->getSourceFileName());
	QString contestantDirName = contestantDir.path();
	QList<Compiler *> compilerList = settings->getCompilerList();

	for (auto &i : compilerList) {
		if (task->getCompilerConfiguration(i->getCompilerName()) == "disable")
			continue;

		QStringList filters;

		if (task->getTaskType() == Task::Communication || task->getTaskType() == Task::CommunicationExec) {
			filters = task->getSourceFilesPath();
		} else {
			filters = i->getSourceExtensions();

			for (int j = 0; j < filters.size(); j++) {
				filters[j] = task->getSourceFileName() + "." + filters[j];
			}
		}

		QStringList files = contestantDir.entryList(filters, QDir::Files);
		sourceFile = "";

		for (int j = 0; j < files.size(); j++) {
			qint64 fileSize = QFileInfo(contestantDirName + QDir::separator() + files[j]).size();
			// Refuse to compile if the source file is too large
			if (fileSize <= settings->getFileSizeLimit() * 1024) {
				if (task->getTaskType() == Task::Communication ||
				    task->getTaskType() == Task::CommunicationExec) {
					sourceFile = sourceFile + " " + files[j] + " ";
				} else {
					sourceFile = files[j];
					break;
				}
			}
		}

		QStringList sourcePaths;
		QStringList sourceNames;
		QStringList graderPaths;
		QStringList graderNames;
		QString mainGraderPath;
		QString mainGraderName;

		if (sourceFile.isEmpty())
			continue;
		QDir(QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator()).mkdir(contestantName);
		// Copy files to temporary dir
		if (task->getTaskType() == Task::Communication || task->getTaskType() == Task::CommunicationExec) {
			sourceFile = "";
			sourcePaths = task->getSourceFilesPath();
			sourceNames = task->getSourceFilesName();

			for (int i = 0; i < sourcePaths.length(); i++) {
				QFile::copy(contestantDirName + QDir::separator() + sourcePaths[i],
				            QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() +
				                contestantName + QDir::separator() + sourceNames[i]);
				sourceFile = sourceFile + " " + sourceNames[i] + " ";
			}
		} else {
			QFile::copy(contestantDirName + QDir::separator() + sourceFile,
			            QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() + contestantName +
			                QDir::separator() + sourceFile);
		}

		QString extraFiles = "";

		if (task->getTaskType() == Task::Interaction) {
			QFile::copy(Settings::dataPath() + task->getInteractor(),
			            QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() + contestantName +
			                QDir::separator() + task->getInteractorName());
			QFile::copy(Settings::dataPath() + task->getGrader(),
			            QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() + contestantName +
			                QDir::separator() + "__grader.cpp");
		}

		if (task->getTaskType() == Task::Communication || task->getTaskType() == Task::CommunicationExec) {
			graderPaths = task->getGraderFilesPath();
			graderNames = task->getGraderFilesName();

			for (int i = 0; i < graderPaths.length(); i++) {
				QFile::copy(Settings::dataPath() + graderPaths[i],
				            QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() +
				                contestantName + QDir::separator() + graderNames[i]);
				extraFiles = extraFiles + " " + graderNames[i] + " ";
			}
		}

		// Get compiler configuration
		QStringList configurationNames = i->getConfigurationNames();
		QStringList compilerArguments = i->getCompilerArguments();
		QStringList interpreterArguments = i->getInterpreterArguments();
		QString currentConfiguration = task->getCompilerConfiguration(i->getCompilerName());

		int configurationIndex =
		    std::find(configurationNames.begin(), configurationNames.end(), currentConfiguration) -
		    configurationNames.begin();

		compilerTimeLimitRatio = i->getTimeLimitRatio();
		compilerMemoryLimitRatio = i->getMemoryLimitRatio();
		disableMemoryLimitCheck = i->getDisableMemoryLimitCheck();
		interpreterAsWatcher = i->getInterpreterAsWatcher();
		sandboxSettings = i->getSandboxSettings();
		runtimeEnvironment = i->getEnvironment();
		runtimeExecutable = i->getCompilerType() == Compiler::Typical ? i->getCompilerLocation()
		                                                              : i->getInterpreterLocation();
		if (sandboxSettings.runtime == SandboxSettings::Automatic) {
			if (i->getCompilerType() == Compiler::Typical)
				sandboxSettings.runtime = SandboxSettings::Native;
			else if (i->getSourceExtensions().contains("py"))
				sandboxSettings.runtime = SandboxSettings::Python;
			else if (i->getSourceExtensions().contains("java"))
				sandboxSettings.runtime = SandboxSettings::Java;
		}
		environment = i->getEnvironment();
		QStringList values = QProcessEnvironment::systemEnvironment().toStringList();

		for (int k = 0; k < values.size(); k++) {
			int tmp = values[k].indexOf("=");

			if (tmp == 0)
				continue;

			QString variable = values[k].mid(0, tmp);
			if (environment.contains(variable))
				// ';' for windows ':' for linux
				environment.insert(variable, environment.value(variable) +
#ifdef Q_OS_WIN32
				                                 ";"
#else
				                                 ":"
#endif
				                                 + QProcessEnvironment::systemEnvironment().value(variable));
			else
				environment.insert(variable, QProcessEnvironment::systemEnvironment().value(variable));
		}

		if (i->getCompilerType() == Compiler::Typical) {
			if (task->getTaskType() == Task::CommunicationExec)
				executableFile = commExecGrader;
			else
				executableFile = task->getSourceFileName();
#ifdef Q_OS_WIN32
			executableFile.append(".exe");
#endif
			interpreterFlag = false;
		} else {
			executableFile = i->getInterpreterLocation();
			arguments = interpreterArguments[configurationIndex];
			arguments.replace("%s.*", sourceFile + extraFiles);
			arguments.replace("%s", task->getSourceFileName());
			interpreterFlag = true;
		}

		if (i->getCompilerType() != Compiler::InterpretiveWithoutByteCode) {
			makeDialogAlert(tr("Compiling..."));

			QStringList arguments;
			arguments.append(compilerArguments[configurationIndex]);

			if (task->getTaskType() == Task::Interaction) {
				arguments[0].replace("%s.*", sourceFile + " __grader.cpp");
				arguments[0].replace("%s", task->getSourceFileName());
			} else if (task->getTaskType() == Task::Communication) {
				arguments[0].replace("%s.*", sourceFile + extraFiles);
				arguments[0].replace("%s", task->getSourceFileName());
			} else if (task->getTaskType() == Task::CommunicationExec) {
				for (int k = 1; k < sourceNames.size(); k++)
					arguments.append(compilerArguments[configurationIndex]);
				for (int k = 0; k < sourceNames.size(); k++) {
					arguments[k].replace("%s.*", sourceNames[k]);
					QString name = sourceNames[k];
					name.truncate(name.lastIndexOf('.'));
					arguments[k].replace("%s", name);
				}

				QStringList filters = i->getSourceExtensions();
				for (auto &k : filters)
					k = commExecGrader + "." + k;
				bool found = 0;
				for (auto k : graderPaths) {
					QString name = k.section(QDir::separator(), -1);
					if (filters.contains(name)) {
						mainGraderPath = k;
						mainGraderName = name;
						found = 1;
						break;
					}
				}
				if (! found) {
					compileState = NoValidGraderFile;
					compileMessage = tr("Main grader (grader.*) cannot be found");
					break;
				}
				// Why? It's unix Only
				auto graderArgument = compilerArguments[configurationIndex] + " -pthread";
				graderArgument.replace("%s.*", mainGraderName);
				graderArgument.replace("%s", commExecGrader);
				arguments.append(graderArgument);
			} else {
				arguments[0].replace("%s.*", sourceFile);
				arguments[0].replace("%s", task->getSourceFileName());
			}

			for (auto k : arguments) {
				QProcess compilerProcess;
				compilerProcess.setProcessChannelMode(QProcess::MergedChannels);
				compilerProcess.setProcessEnvironment(environment);
				compilerProcess.setWorkingDirectory(QDir::toNativeSeparators(temporaryDir.path()) +
				                                    QDir::separator() + contestantName);
				// TODO: 需要重构代码来处理含空格路径问题

				compilerProcess.start(i->getCompilerLocation(),
				                      k.split(QLatin1Char(' '), Qt::SkipEmptyParts));

				if (! compilerProcess.waitForStarted(-1)) {
					compileState = InvalidCompiler;
					break;
				}

				QElapsedTimer timer;
				timer.start();
				bool flag = false;

				while (timer.elapsed() < settings->getCompileTimeLimit()) {
					if (compilerProcess.waitForFinished(10)) {
						flag = true;
						break;
					}

					QCoreApplication::processEvents();

					if (! isJudging) {
						compilerProcess.kill();
						return false;
					}
				}

				if (! flag) {
					compilerProcess.kill();
					compileState = CompileTimeLimitExceeded;
					// Stop at the first failure: a later command in the batch (a
					// communication task compiles several files) succeeding must not
					// overwrite the timeout or hide the error message.
					break;
				} else if (compilerProcess.exitCode() != 0) {
					compileState = CompileError;
					compileMessage =
					    QString::fromLocal8Bit(compilerProcess.readAllStandardOutput().constData());
					break;
				} else {
					if (i->getCompilerType() == Compiler::Typical) {
						if (! QDir(QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() +
						           contestantName)
						          .exists(executableFile)) {
							compileState = InvalidCompiler;
						} else {
							compileState = CompileSuccessfully;
						}
					} else {
						QStringList filters = i->getBytecodeExtensions();

						for (int k = 0; k < filters.size(); k++) {
							filters[k] = QString("*.") + filters[k];
						}

						if (QDir(QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() +
						         contestantName)
						        .entryList(filters, QDir::Files)
						        .empty()) {
							compileState = InvalidCompiler;
						} else {
							compileState = CompileSuccessfully;
						}
					}
				}
			}

			// Only a compile that actually succeeded gets announced: the loop above
			// breaks out on a compile error or a missing compiler after setting
			// compileState, and announcing success there used to mislead the contestant.
			if (compileState == CompileSuccessfully)
				makeDialogAlert(tr("Compiled Successfully"));
		}

		if (i->getCompilerType() == Compiler::InterpretiveWithoutByteCode)
			compileState = CompileSuccessfully;

		// One file compiled, skip all other files
		break;
	}

	if (compileState != CompileSuccessfully) {
		emit compileError(task->getTotalTimeLimit(), static_cast<int>(compileState));
		return false;
	}

	return true;
}

void TaskJudger::judgeIt() {
	qDebug() << "Start Judging";
	emit judgingStarted(task->getProblemTitle());
	const bool judged = judge();
	// Contestant is a main-thread object and its containers are read by the UI while
	// judging runs. Apply the results on the owning thread so a worker never mutates
	// state the UI is reading; blocking keeps `this` (and its result members) alive
	// until the write completes.
	if (contestant) {
		const auto type = QThread::currentThread() == contestant->thread() ? Qt::DirectConnection
		                                                                   : Qt::BlockingQueuedConnection;
		QMetaObject::invokeMethod(
		    contestant,
		    [this, judged]() {
			    if (judged) {
				    contestant->setCheckJudged(taskId, true);
				    contestant->setCompileMessage(taskId, compileMessage);
				    contestant->setCompileState(taskId, compileState);
				    contestant->setResult(taskId, result);
				    contestant->setMessage(taskId, message);
				    contestant->setTimeUsed(taskId, timeUsed);
				    contestant->setMemoryUsed(taskId, memoryUsed);
				    contestant->setScore(taskId, score);
				    contestant->setInputFiles(taskId, inputFiles);
				    contestant->setSourceFile(taskId, sourceFile);
			    } else {
				    contestant->setCheckJudged(taskId, false);
			    }
		    },
		    type);
	}
	emit judgingFinished();
}

int TaskJudger::judge() {
	isJudging = true;
	QString contestantName = contestant->getContestantName();
	if (! temporaryDir.isValid())
		return 0;

	for (int i = 0; i < task->getTestCaseList().size(); i++) {
		timeUsed.append(QList<int>());
		memoryUsed.append(QList<qint64>());
		score.append(QList<int>());
		result.append(QList<ResultState>());
		overallStatus.append(maxDependValue);
		message.append(QStringList());
		inputFiles.append(QStringList());
		testCaseScore.append(task->getTestCase(i)->getFullScore());

		for (int j = 0; j < task->getTestCase(i)->getInputFiles().size(); j++) {
			timeUsed[i].append(-1);
			memoryUsed[i].append(-1);
			score[i].append(0);
			result[i].append(Skipped);
			message[i].append("");
			inputFiles[i].append("");
		}
	}

	if (task->getTaskType() != Task::AnswersOnly) {
		if (! traditionalTaskPrepare()) {
			// traditionalTaskPrepare() has already reported what went wrong
			// (no usable source file, the compiler refused the code, the
			// compile time limit was hit, ...).  The task still counts as
			// judged, but leaving the result table empty made a broken
			// submission - or a broken tool chain - look like a judged task
			// without any results, so record the failure for every test case.
			for (int i = 0; i < result.size(); i++)
				for (int j = 0; j < result[i].size(); j++)
					result[i][j] = CompileFailed;

			return 1;
		}
	}

	std::shared_ptr<WindowsSandboxSession> sandboxSession;
#ifdef Q_OS_WIN
	if (sandboxSettings.enabled)
		sandboxSession = WindowsSandbox::createSession();
#endif

	// A Windows sandbox session is a shared, ACL-backed resource.  Its preparation is
	// serialized inside WindowsSandbox, but every case gets its own AppContainer profile
	// and the running processes share nothing, so cases may overlap.
	int workers = caseWorkers;
	workers = qMax(1, workers);

	const int subtaskCount = task->getTestCaseList().size();
	// Verdicts are staged here and copied into the result tables by the replay below;
	// that is what keeps the accumulation and the signals in their original order.
	QList<QList<CaseOutcome>> outcomes;
	for (int i = 0; i < subtaskCount; i++)
		outcomes.append(QList<CaseOutcome>(task->getTestCase(i)->getInputFiles().size()));

	for (int first = 0; first < subtaskCount;) {
		QCoreApplication::processEvents();
		if (! isJudging) {
			return 0;
		}

		// Which sub tasks may be judged together: one that depends on a sub task of the
		// same group would read that dependency's status before it is final, so it has
		// to wait for the next group.  A dependence on a later sub task reads as full
		// score - the sequential loop did the same, because that entry was still at its
		// initial maxDependValue - so it holds nothing up.  Without dependence (the
		// common case) this makes one group out of the whole task, and all of its test
		// cases can then run at once.
		QList<int> group{first};
		int next = first + 1;
		while (next < subtaskCount) {
			bool blocked = false;
			const QList<int> &dependenceSubtask(task->getTestCase(next)->getDependenceSubtask());
			for (int dep : dependenceSubtask)
				if (dep - 1 >= first && dep - 1 < next)
					blocked = true;
			if (blocked)
				break;
			group.append(next);
			++next;
		}

		// Dependence pre-pass for the whole group, in index order.  It judges nothing,
		// so it stays on this thread.
		QList<int> runnable;
		for (int i : group) {
			auto curTestCase = task->getTestCase(i);
			const QList<int> &dependenceSubtask(curTestCase->getDependenceSubtask());

			overallStatus[i] = maxDependValue;

			bool isSkipped = false;

			for (int j = 0; j != dependenceSubtask.size(); ++j) {
				int status = overallStatus[dependenceSubtask[j] - 1];
				emit singleSubtaskDependenceFinished(i, dependenceSubtask[j], status);

				if (status < 0)
					isSkipped = true;

				overallStatus[i] = qMin(overallStatus[i], status);
			}

			if (! dependenceSubtask.empty())
				score[i].push_back(overallStatus[i]);

			if (! isSkipped)
				runnable.append(i);
		}

		// Judge every case of the group's runnable sub tasks, on `workers` threads.
		if (! runnable.isEmpty() && isJudging) {
			QList<std::pair<int, int>> units;
			for (int i : runnable)
				for (int j = 0; j < task->getTestCase(i)->getInputFiles().size(); j++)
					units.append(std::make_pair(i, j));

			std::atomic<int> nextUnit{0};
			auto worker = [&]() {
				for (;;) {
					if (! isJudging)
						return;
					int index = nextUnit.fetch_add(1);
					if (index >= units.size())
						return;
					judgeSingleCase(units[index].first, units[index].second, contestantName, sandboxSession,
					                outcomes);
				}
			};

			int threadCount = qMin(workers, int(units.size()));
			if (threadCount <= 1) {
				worker();
			} else {
				std::vector<std::thread> pool;
				pool.reserve(threadCount);
				for (int t = 0; t < threadCount; ++t)
					pool.emplace_back(worker);
				for (auto &entry : pool)
					entry.join();
			}
		}

		// Replay the verdicts in index order.  The result tables, the score
		// accumulation, the poison rule and the signals all live here, so their
		// behaviour is unchanged; a case that a stop or the poison rule skipped is never
		// committed, exactly as it was never written before.
		for (int i : runnable) {
			auto curTestCase = task->getTestCase(i);

			for (int j = 0; j < curTestCase->getInputFiles().size(); j++) {
				if (! isJudging)
					return 0;

				inputFiles[i][j] = QFileInfo(curTestCase->getInputFiles().at(j)).fileName();

				testCaseScore[i] =
				    qMin(testCaseScore[i], statusToScore(overallStatus[i], curTestCase->getFullScore()));

				// A case that scored nothing poisons the rest of its sub task.
				if (overallStatus[i] < 0) {
					overallStatus[i] = -1;
					taskSkipped(std::make_pair(i, j++));
					break;
				}

				CaseOutcome &outcome = outcomes[i][j];

				if (outcome.mkdirFailed) {
					result[i][j] = FileError;
					message[i][j] = outcome.message;
					overallStatus[i] = -1;
					testCaseScore[i] = 0;
					emit singleCaseFinished(contestantName, curTestCase->getTimeLimit(), i, j, int(FileError),
					                        0, timeUsed[i][j], memoryUsed[i][j]);
					continue;
				}

				timeUsed[i][j] = outcome.timeUsed;
				memoryUsed[i][j] = outcome.memoryUsed;
				score[i][j] = outcome.score;
				result[i][j] = outcome.result;
				overallStatus[i] = qMin(overallStatus[i], stateToStatus(outcome.result, outcome.score,
				                                                        curTestCase->getFullScore()));
				message[i][j] = outcome.message;
				int nowScore = score[i][j];

				if (j + 1 == curTestCase->getInputFiles().size()) {
					for (int k = 0; k < j; k++)
						nowScore = qMin(nowScore, score[i][k]);

					if (! curTestCase->getDependenceSubtask().empty())
						nowScore =
						    qMin(nowScore, statusToScore(overallStatus[i], curTestCase->getFullScore()));
				}

				emit singleCaseFinished(contestantName, curTestCase->getTimeLimit(), i, j, int(result[i][j]),
				                        (j + 1 == curTestCase->getInputFiles().size() ? 1 : -1) * nowScore,
				                        timeUsed[i][j], memoryUsed[i][j]);

				if (score[i][j] < testCaseScore[i])
					testCaseScore[i] = score[i][j];
			}
		}

		first = next;
	}

	return 1;
}

// Judge one test case.  This runs on a worker thread, so it must not touch the result
// tables, emit signals or pump the event loop - judge() replays the verdict on the
// judging thread instead.  What it does do is register its JudgingThread, so that
// stop() interrupts the process rather than waiting for the case's own time limit.
void TaskJudger::judgeSingleCase(int subtask, int index, const QString &contestantName,
                                 const std::shared_ptr<WindowsSandboxSession> &sandboxSession,
                                 QList<QList<CaseOutcome>> &outcomes) {
	auto curTestCase = task->getTestCase(subtask);
	CaseOutcome &outcome = outcomes[subtask][index];
	QString workingDirectory =
	    QDir::toNativeSeparators(QDir(QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() +
	                                  QString("_%1.%2").arg(subtask).arg(index))
	                                 .absolutePath()) +
	    QDir::separator();
	if (! QDir(temporaryDir.path()).mkdir(QString("_%1.%2").arg(subtask).arg(index))) {
		outcome.mkdirFailed = true;
		outcome.message = tr("Cannot create testcase working directory: %1").arg(workingDirectory);
		return;
	}

	auto *thread = new JudgingThread();
	// 上游把建目录检查放到了建 JudgingThread 之前，这里沿用它的顺序：mkdir 失败时不会白建一个对象。
	// 但 stop() 仍然需要在用例真正开始跑之前拿到指针，所以对象一建好就登记下来。
	registerJudgingThread(thread);
	thread->setExtraTimeRatio(settings->getDefaultExtraTimeRatio());
	thread->setWorkingDirectory(workingDirectory);
	thread->setSandboxConfiguration(sandboxSettings, runtimeExecutable, runtimeEnvironment, sandboxSession);
	QStringList entryList =
	    QDir(QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() + contestantName)
	        .entryList(QDir::Files);

	for (int fileIdx = 0; fileIdx < entryList.size(); fileIdx++) {
		QFile::copy(QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() + contestantName +
		                QDir::separator() + entryList[fileIdx],
		            workingDirectory + entryList[fileIdx]);
	}

	thread->setSpecialJudgeTimeLimit(settings->getSpecialJudgeTimeLimit());
	thread->setDiffPath(settings->getDiffPath());

	if (task->getTaskType() == Task::Traditional || task->getTaskType() == Task::Interaction ||
	    task->getTaskType() == Task::Communication || task->getTaskType() == Task::CommunicationExec) {
		if (interpreterFlag) {
			thread->setExecutableFile(executableFile);
		} else {
			thread->setExecutableFile(workingDirectory + executableFile);
		}

		thread->setArguments(arguments);
	}

	if (task->getTaskType() == Task::AnswersOnly) {
		QString fileName;
		fileName = QFileInfo(curTestCase->getInputFiles().at(index)).completeBaseName();
		fileName += QString(".") + task->getAnswerFileExtension();

		if (! task->getSubFolderCheck())
			thread->setAnswerFile(Settings::sourcePath() + contestantName + QDir::separator() + fileName);
		else
			thread->setAnswerFile(Settings::sourcePath() + contestantName + QDir::separator() +
			                      task->getSourceFileName() + QDir::separator() + fileName);
	}

	thread->setTask(task);
	thread->setInputFile(Settings::dataPath() + curTestCase->getInputFiles().at(index));
	thread->setOutputFile(Settings::dataPath() + curTestCase->getOutputFiles().at(index));
	thread->setFullScore(curTestCase->getFullScore());

	if (task->getTaskType() != Task::AnswersOnly) {
		thread->setEnvironment(environment);
		thread->setTimeLimit(qCeil(curTestCase->getTimeLimit() * compilerTimeLimitRatio));
		thread->setRawTimeLimit(qCeil(curTestCase->getTimeLimit()));

		if (disableMemoryLimitCheck) {
			thread->setMemoryLimit(-1);
		} else {
			thread->setMemoryLimit(qCeil(curTestCase->getMemoryLimit() * compilerMemoryLimitRatio));
		}
		thread->setRawMemoryLimit(curTestCase->getMemoryLimit());

		thread->setInterpreterAsWatcher(interpreterAsWatcher);
	}

	// judgeNow() rather than start()/wait(): the pool that called us is already the
	// concurrency, so a child thread that is started and immediately waited for only
	// buys a thread creation per test case.
	thread->judgeNow();

	// No QCoreApplication::processEvents() here: it used to be needed because a stop
	// request arrived as a queued call, which only a pumping event loop would deliver.
	// stop() is direct now, and pumping the event loop from a worker thread would be
	// wrong (it could deliver calls into code that is not thread safe).
	while (isJudging && thread->getNeedRejudge() &&
	       thread->getJudgeTimes() != settings->getRejudgeTimes() + 1) {
		thread->judgeNow();
	}

	if (isJudging) {
		outcome.timeUsed = thread->getTimeUsed();
		outcome.memoryUsed = thread->getMemoryUsed();
		outcome.score = thread->getScore();
		outcome.result = thread->getResult();
		outcome.message = thread->getMessage();
	}

	unregisterJudgingThread(thread);
	delete thread;
}

void TaskJudger::makeDialogAlert(QString msg) { emit dialogAlert(std::move(msg)); }
void TaskJudger::taskSkipped(const std::pair<int, int> &cur) {
	emit singleCaseFinished(contestant->getContestantName(), task->getTestCase(cur.first)->getTimeLimit(),
	                        cur.first, cur.second, int(result[cur.first][cur.second]), 0, 0, 0);
}

void TaskJudger::setCaseWorkers(int workers) { caseWorkers = qMax(1, workers); }

void TaskJudger::registerJudgingThread(JudgingThread *thread) {
	QMutexLocker locker(&currentThreadMutex);
	runningJudgingThreads.append(thread);
}

void TaskJudger::unregisterJudgingThread(JudgingThread *thread) {
	QMutexLocker locker(&currentThreadMutex);
	runningJudgingThreads.removeAll(thread);
}

void TaskJudger::stop() {
	isJudging = false;
	// Also interrupt the processes running the current test cases; otherwise stopping
	// would wait until each of them reached its own time limit.
	//
	// This is reached through JudgingController::stop() with Qt::DirectConnection,
	// so it runs on the caller's thread while the judging threads are blocked inside
	// QThread::wait() and cannot process queued calls.  The mutex is what makes that
	// safe: it keeps the pointers alive across the dereference, because every
	// JudgingThread is unregistered before it is deleted.
	QMutexLocker locker(&currentThreadMutex);
	for (auto *thread : std::as_const(runningJudgingThreads))
		if (thread)
			thread->stop();
}

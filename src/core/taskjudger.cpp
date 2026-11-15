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
#include "core/casejudger.h"
#include "core/subtaskdependencelib.h"
#include "core/task.h"
#include "core/testcase.h"
#ifdef Q_OS_WIN
#include "core/windowssandbox.h"
#endif

#include <QSysInfo>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#define LEMON_MODULE_NAME "TaskJudger"

TaskJudger::TaskJudger(QObject *parent) : QObject(parent) { compileState = NoValidSourceFile; }

void TaskJudger::setSettings(const Settings *_settings) { settings = _settings; }

void TaskJudger::setTask(const Task *_task) { task = _task; }

void TaskJudger::setContestantName(QString name) { contestantName = std::move(name); }

void TaskJudger::setCaseWorkers(int workers) { caseWorkers = qMax(1, workers); }

// Get executable file
auto TaskJudger::traditionalTaskPrepare() -> bool {
	makeDialogAlert(tr("Preparing..."));

	// Get the source code of contestant
	compileState = NoValidSourceFile;
	QDir contestantDir;
	contestantDir =
	    ! task->getSubFolderCheck()
	        ? QDir(Settings::sourcePath() + contestantName)
	        : QDir(Settings::sourcePath() + contestantName + QDir::separator() + task->getSourceFileName());
	QString contestantDirName = contestantDir.path();
	const QList<Compiler *> &compilerList = settings->getCompilerList();

	for (const Compiler *i : compilerList) {
		if (cancellation.isCanceled())
			return false;
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
				arguments.append(graderArgument);
				graderArgument.replace("%s.*", mainGraderName);
				graderArgument.replace("%s", commExecGrader);
				arguments.append(graderArgument);
			} else {
				arguments[0].replace("%s.*", sourceFile);
				arguments[0].replace("%s", task->getSourceFileName());
			}

			for (auto k : arguments) {
				if (cancellation.isCanceled())
					return false;
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

				while (timer.elapsed() < settings->getCompileTimeLimit() && ! cancellation.isCanceled()) {
					if (compilerProcess.waitForFinished(10)) {
						flag = true;
						break;
					}
				}

				if (! flag) {
					compilerProcess.kill();
					compileState = CompileTimeLimitExceeded;
				} else if (compilerProcess.exitCode() != 0) {
					compileState = CompileError;
					compileMessage =
					    QString::fromLocal8Bit(compilerProcess.readAllStandardOutput().constData());
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

	if (cancellation.isCanceled())
		return false;
	if (compileState != CompileSuccessfully) {
		emit compileError(task->getTotalTimeLimit(), static_cast<int>(compileState));
		return false;
	}

	return true;
}

TaskResult TaskJudger::judgeIt(QPromise<TaskResult> &promise) {
	cancellation = promise.future();
	if (cancellation.isCanceled())
		return {};
	qDebug() << "Start Judging";
	emit judgingStarted(task->getProblemTitle());
	const bool judged = judge() && ! cancellation.isCanceled();
	return {.judged = judged,
	        .compileState = compileState,
	        .compileMessage = std::move(compileMessage),
	        .sourceFile = std::move(sourceFile),
	        .inputFiles = std::move(inputFiles),
	        .result = std::move(result),
	        .message = std::move(message),
	        .score = std::move(score),
	        .timeUsed = std::move(timeUsed),
	        .memoryUsed = std::move(memoryUsed)};
}

int TaskJudger::judge() {
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
	// serialized inside WindowsSandbox, and each running case borrows its own AppContainer
	// profile from the session, so cases may overlap.
	const int workers = qMax(1, caseWorkers);

	const int subtaskCount = task->getTestCaseList().size();
	// Verdicts are staged here and copied into the result tables by the replay below;
	// that is what keeps the accumulation and the signals in their original order.
	QList<QList<CaseOutcome>> outcomes;
	for (int i = 0; i < subtaskCount; i++)
		outcomes.append(QList<CaseOutcome>(task->getTestCase(i)->getInputFiles().size()));

	for (int first = 0; first < subtaskCount;) {
		if (cancellation.isCanceled())
			return 0;

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
		if (! runnable.isEmpty() && ! cancellation.isCanceled()) {
			QList<std::pair<int, int>> units;
			for (int i : runnable)
				for (int j = 0; j < task->getTestCase(i)->getInputFiles().size(); j++)
					units.append(std::make_pair(i, j));

			std::atomic<int> nextUnit{0};
			auto worker = [&]() {
				for (;;) {
					if (cancellation.isCanceled())
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
				if (cancellation.isCanceled())
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

	auto caseJudger = std::make_unique<CaseJudger>();
	caseJudger->setCancellation(cancellation);
	caseJudger->setExtraTimeRatio(settings->getDefaultExtraTimeRatio());
	caseJudger->setWorkingDirectory(workingDirectory);
	caseJudger->setSandboxConfiguration(sandboxSettings, runtimeExecutable, runtimeEnvironment,
	                                    sandboxSession);
	QStringList entryList =
	    QDir(QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() + contestantName)
	        .entryList(QDir::Files);

	for (int fileIdx = 0; fileIdx < entryList.size(); fileIdx++) {
		QFile::copy(QDir::toNativeSeparators(temporaryDir.path()) + QDir::separator() + contestantName +
		                QDir::separator() + entryList[fileIdx],
		            workingDirectory + entryList[fileIdx]);
	}

	caseJudger->setSpecialJudgeTimeLimit(settings->getSpecialJudgeTimeLimit());
	caseJudger->setDiffPath(settings->getDiffPath());

	if (task->getTaskType() == Task::Traditional || task->getTaskType() == Task::Interaction ||
	    task->getTaskType() == Task::Communication || task->getTaskType() == Task::CommunicationExec) {
		if (interpreterFlag) {
			caseJudger->setExecutableFile(executableFile);
		} else {
			caseJudger->setExecutableFile(workingDirectory + executableFile);
		}

		caseJudger->setArguments(arguments);
	}

	if (task->getTaskType() == Task::AnswersOnly) {
		QString fileName;
		fileName = QFileInfo(curTestCase->getInputFiles().at(index)).completeBaseName();
		fileName += QString(".") + task->getAnswerFileExtension();

		if (! task->getSubFolderCheck())
			caseJudger->setAnswerFile(Settings::sourcePath() + contestantName + QDir::separator() + fileName);
		else
			caseJudger->setAnswerFile(Settings::sourcePath() + contestantName + QDir::separator() +
			                          task->getSourceFileName() + QDir::separator() + fileName);
	}

	caseJudger->setTask(task);
	caseJudger->setInputFile(Settings::dataPath() + curTestCase->getInputFiles().at(index));
	caseJudger->setOutputFile(Settings::dataPath() + curTestCase->getOutputFiles().at(index));
	caseJudger->setFullScore(curTestCase->getFullScore());

	if (task->getTaskType() != Task::AnswersOnly) {
		caseJudger->setEnvironment(environment);
		caseJudger->setTimeLimit(qCeil(curTestCase->getTimeLimit() * compilerTimeLimitRatio));
		caseJudger->setRawTimeLimit(qCeil(curTestCase->getTimeLimit()));

		if (disableMemoryLimitCheck) {
			caseJudger->setMemoryLimit(-1);
		} else {
			caseJudger->setMemoryLimit(qCeil(curTestCase->getMemoryLimit() * compilerMemoryLimitRatio));
		}
		caseJudger->setRawMemoryLimit(curTestCase->getMemoryLimit());

		caseJudger->setInterpreterAsWatcher(interpreterAsWatcher);
	}

	caseJudger->run();

	// Rejudge on demand, but never after a stop request: the controller discards the
	// whole task result in that case, so another round would only delay the stop.
	while (! cancellation.isCanceled() && caseJudger->getNeedRejudge() &&
	       caseJudger->getJudgeTimes() != settings->getRejudgeTimes() + 1) {
		caseJudger->run();
	}

	if (! cancellation.isCanceled()) {
		outcome.timeUsed = caseJudger->getTimeUsed();
		outcome.memoryUsed = caseJudger->getMemoryUsed();
		outcome.score = caseJudger->getScore();
		outcome.result = caseJudger->getResult();
		outcome.message = caseJudger->getMessage();
	}
}

void TaskJudger::makeDialogAlert(QString msg) { emit dialogAlert(std::move(msg)); }
void TaskJudger::taskSkipped(const std::pair<int, int> &cur) {
	emit singleCaseFinished(contestantName, task->getTestCase(cur.first)->getTimeLimit(), cur.first,
	                        cur.second, int(result[cur.first][cur.second]), 0, 0, 0);
}

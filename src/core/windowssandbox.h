/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once

#include "processrunner.h"
#ifdef Q_OS_WIN
#include <windows.h>

class WindowsSandbox {
  public:
	static std::shared_ptr<WindowsSandboxSession> createSession();
	explicit WindowsSandbox(const ProcessRunnerConfig &config);
	~WindowsSandbox();
	WindowsSandbox(const WindowsSandbox &) = delete;
	WindowsSandbox &operator=(const WindowsSandbox &) = delete;
	bool prepare(QString &error);
	bool prepareProcess(STARTUPINFOEXW &startup, QString &error);
	const QProcessEnvironment &environment() const;
	int aclUpdates() const;
	bool cacheHit() const;
	// True when the case borrowed an AppContainer identity the session had already used.
	bool profileReused() const;

	// Wall-clock split of prepare(), for the judging log: creating and deleting the
	// AppContainer profile, granting the runtime directories, and granting the
	// per-case working directory.  Diagnostics only.
	struct PreparationTiming {
		qint64 profileMs = 0;
		qint64 permissionsMs = 0;
		qint64 filesMs = 0;
	};
	PreparationTiming preparationTiming() const;

  private:
	struct Data;
	std::unique_ptr<Data> data;
};
#endif

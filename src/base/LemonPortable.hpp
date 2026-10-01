/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#pragma once
//
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
//
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

// A "portable" (green) copy of LemonLime keeps every piece of its state inside
// the folder it was unpacked into: settings live in <appdir>/LemonLime/lemon.ini
// and logs in <appdir>/logs, instead of the registry and the user profile.
//
// Portable mode is enabled by shipping a `portable` marker file next to the
// executable (the windows-portable CMake target does that for you). When the
// folder is not writable - for instance when it was unpacked into Program Files -
// LemonLime silently falls back to the regular per-user locations, so nothing
// ever breaks because of a read-only directory. Setting LEMON_PORTABLE=0
// disables the detection entirely.

namespace Lemon::Portable {
	// The portable layout has to be known before the QApplication object exists,
	// because the logger (which lives inside the portable folder) is created
	// first, so the application directory cannot be asked from QCoreApplication
	// at that point. Fall back to the platform's own way of locating the running
	// executable.
	inline auto appDir() -> QString {
		if (QCoreApplication::instance() != nullptr)
			return QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
		wchar_t buffer[MAX_PATH];

		if (const auto length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(MAX_PATH));
		    length > 0 && length < static_cast<DWORD>(MAX_PATH))
			return QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(length))).absolutePath();
#elif defined(Q_OS_LINUX)
		const auto target = QFile::symLinkTarget(QStringLiteral("/proc/self/exe"));

		if (! target.isEmpty())
			return QFileInfo(target).absolutePath();
#endif
		return QCoreApplication::applicationDirPath();
	}

	inline auto markers() -> QStringList {
		return {QStringLiteral("portable"), QStringLiteral("lemon.ini"), QStringLiteral("LemonLime.ini")};
	}

	inline auto isWritable(const QString &path) -> bool {
		QDir dir(path);

		if (! dir.exists() && ! QDir().mkpath(path))
			return false;

		QFile probe(dir.filePath(QStringLiteral(".lemon-write-test")));

		if (! probe.open(QIODevice::WriteOnly))
			return false;

		probe.close();
		probe.remove();
		return true;
	}

	inline auto detect() -> bool {
		const auto override = qEnvironmentVariable("LEMON_PORTABLE");

		if (! override.isEmpty())
			return override != QLatin1String("0") &&
			       override.compare(QLatin1String("false"), Qt::CaseInsensitive) != 0;

		const auto dir = appDir();

		for (const auto &marker : markers()) {
			if (QFileInfo::exists(dir + QLatin1Char('/') + marker))
				return isWritable(dir);
		}

		return false;
	}

	// Whether this run uses the portable layout. Decided once, at startup.
	inline auto isEnabled() -> bool {
		static const bool enabled = detect();
		return enabled;
	}

	inline auto settingsDirectory() -> QString { return appDir() + QLatin1String("/LemonLime"); }

	// Where the application settings live. QSettings("LemonLime", "lemon") always
	// uses the native format - the registry on Windows - no matter what
	// setDefaultFormat() was told, so portable mode has to ask for the INI format
	// explicitly. Every settings construction site goes through here; outside
	// portable mode this is the very same object the plain constructor made.
	inline auto settings() -> QSettings {
		if (isEnabled())
			return QSettings(QSettings::IniFormat,
			                 QSettings::UserScope,
			                 QStringLiteral("LemonLime"),
			                 QStringLiteral("lemon"));

		return QSettings(QStringLiteral("LemonLime"), QStringLiteral("lemon"));
	}

	inline auto logDirectory() -> QString {
		if (isEnabled())
			return appDir() + QLatin1String("/logs");

		return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
		       QLatin1String("/logs");
	}

	// Directories a shipped compiler/toolchain may live in, e.g.
	// <appdir>/compiler/mingw64/bin/g++.exe.
	inline auto bundledToolDirectories() -> QStringList {
		QStringList result;
		const QDir root(appDir() + QLatin1String("/compiler"));

		if (! root.exists())
			return result;

		const auto takeIfTool = [&result](const QDir &dir) {
			const QDir bin(dir.filePath(QStringLiteral("bin")));

			if (! bin.exists())
				return;

			// Only a directory that really holds a tool counts as a toolchain; an
			// empty or half-copied layout must not end up on the PATH.
			for (const auto &tool : {QStringLiteral("g++"), QStringLiteral("gcc"), QStringLiteral("python")}) {
				if (QFileInfo::exists(bin.filePath(tool)) ||
				    QFileInfo::exists(bin.filePath(tool + QStringLiteral(".exe")))) {
					result << bin.absolutePath();
					return;
				}
			}
		};

		takeIfTool(root);

		for (const auto &firstLevel : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			const QDir firstDir(root.filePath(firstLevel));
			takeIfTool(firstDir);

			for (const auto &secondLevel : firstDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
				takeIfTool(QDir(firstDir.filePath(secondLevel)));
		}

		return result;
	}

	// Makes a bundled toolchain discoverable by QStandardPaths::findExecutable(),
	// which is what the compiler wizard uses to pre-fill g++/python/... paths.
	inline void addBundledToolsToPath() {
		const auto directories = bundledToolDirectories();

		if (directories.isEmpty())
			return;

		const auto separator = QDir::listSeparator();
		const auto path = qEnvironmentVariable("PATH");
		const auto existing = path.split(separator, Qt::SkipEmptyParts);
		QStringList prepend;

		for (const auto &directory : directories) {
			const auto native = QDir::toNativeSeparators(directory);

			if (! existing.contains(native, Qt::CaseInsensitive))
				prepend << native;
		}

		if (prepend.isEmpty())
			return;

		qputenv("PATH", (prepend.join(separator) + separator + path).toUtf8());
	}

	// Must be called as soon as a QCoreApplication instance exists and *before*
	// the first QSettings object is constructed, because QSettings remembers the
	// format/path that was current when it was created.
	inline void Initialize() {
		if (isEnabled()) {
			// Settings written through settings() end up in
			// <appdir>/LemonLime/lemon.ini; the default format additionally covers
			// the few places that construct a QSettings without arguments.
			QSettings::setDefaultFormat(QSettings::IniFormat);
			QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, appDir());
		}

		addBundledToolsToPath();
	}
} // namespace Lemon::Portable

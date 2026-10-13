/*
 * SPDX-FileCopyrightText: 2019-2022 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Portable ("green") layout: where a contest dialog starts browsing.  No fixture
// and no compiler are involved, so this target runs everywhere.

#include "base/LemonPortable.hpp"

#include <QDir>
#include <QtTest>

// Lemon::Portable::isEnabled() caches the detection for the whole process - it is
// deliberately decided once, at startup, so that the settings backend cannot change
// underneath a running instance.  The two layouts therefore cannot both be pinned
// from a single run: tests/portable-layout/CMakeLists.txt registers this binary
// twice, with LEMON_PORTABLE=1 and LEMON_PORTABLE=0, and each run is named after the
// slot it is allowed to check.
class PortableLayoutTest : public QObject {
	Q_OBJECT

  private slots:
	void testPortableLayout() {
		QVERIFY2(Lemon::Portable::isEnabled(), "LEMON_PORTABLE=1 did not enable portable mode");

		// A green copy is self-contained, so its contest browser has to start
		// inside the folder it was unpacked into rather than in the host
		// machine's home directory.  The fixture ships no <appdir>/contests
		// folder, so the executable's own directory is the expected answer.
		const QString expected = Lemon::Portable::appDir();
		QCOMPARE(Lemon::Portable::contestBrowseDirectory(), expected);
		QVERIFY2(! expected.isEmpty(), "The application directory was not resolvable");
	}

	void testDefaultLayout() {
		QVERIFY2(! Lemon::Portable::isEnabled(), "LEMON_PORTABLE=0 did not disable portable mode");

		// Outside portable mode the historical default is kept, so the change
		// is invisible to existing installations.
		QCOMPARE(Lemon::Portable::contestBrowseDirectory(), QDir::homePath());
	}
};

QTEST_GUILESS_MAIN(PortableLayoutTest)
#include "main.moc"

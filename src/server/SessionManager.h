/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

class SessionManager : public QObject {
	Q_OBJECT
  public:
	explicit SessionManager(QObject *parent = nullptr);

	QString createSession(const QString &username);
	bool validate(const QString &token, QString *username = nullptr);
	void destroy(const QString &token);
	void destroyAllForUser(const QString &username);

	void setTtl(int seconds) { ttlSeconds_ = seconds; }

  private:
	struct Session {
		QString username;
		QDateTime expiresAt;
	};
	QHash<QString, Session> sessions_;
	int ttlSeconds_ = 6 * 60 * 60; // 6 hours

	void purgeExpired();
};

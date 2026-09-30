/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

class UserStore : public QObject {
	Q_OBJECT
  public:
	struct User {
		QString username;
		QString displayName;
		QByteArray salt;
		QByteArray passwordHash;
		int iterations = 100000;
		QString plaintext; // kept for teacher convenience; classroom threat model
	};

	struct GeneratedUser {
		QString username;
		QString displayName;
		QString plaintextPassword;
	};

	explicit UserStore(QObject *parent = nullptr);

	bool loadFromContestDir(const QString &contestDir);
	bool saveToContestDir(const QString &contestDir) const;

	bool verify(const QString &username, const QString &password) const;
	bool exists(const QString &username) const;
	QString displayNameOf(const QString &username) const;
	QString plaintextOf(const QString &username) const;
	QList<QString> allUsernames() const;
	int count() const;

	QList<GeneratedUser> generateBatch(int count, const QString &prefix, int passwordLen = 8);
	bool addUser(const QString &username, const QString &displayName, const QString &plaintextPassword);
	bool removeUser(const QString &username);
	void clear();

  private:
	QMap<QString, User> users_;
	mutable QString lastError_;

	static QByteArray makeSalt(int len = 16);
	static QString makePassword(int len);
	static QByteArray pbkdf2(const QString &password, const QByteArray &salt, int iterations);
};

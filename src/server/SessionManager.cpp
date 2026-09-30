/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SessionManager.h"

#include <QRandomGenerator>

SessionManager::SessionManager(QObject *parent) : QObject(parent) {}

QString SessionManager::createSession(const QString &username) {
	purgeExpired();
	QByteArray buf(32, Qt::Uninitialized);
	auto *gen = QRandomGenerator::system();
	for (int i = 0; i < buf.size(); ++i)
		buf[i] = static_cast<char>(gen->bounded(0, 256));
	const auto token = QString::fromLatin1(buf.toHex());
	Session s{username, QDateTime::currentDateTimeUtc().addSecs(ttlSeconds_)};
	sessions_.insert(token, s);
	return token;
}

bool SessionManager::validate(const QString &token, QString *username) {
	const auto it = sessions_.find(token);
	if (it == sessions_.end())
		return false;
	if (it->expiresAt < QDateTime::currentDateTimeUtc()) {
		sessions_.erase(it);
		return false;
	}
	// sliding window: extend on access
	it->expiresAt = QDateTime::currentDateTimeUtc().addSecs(ttlSeconds_);
	if (username)
		*username = it->username;
	return true;
}

void SessionManager::destroy(const QString &token) { sessions_.remove(token); }

void SessionManager::destroyAllForUser(const QString &username) {
	for (auto it = sessions_.begin(); it != sessions_.end();) {
		if (it->username == username)
			it = sessions_.erase(it);
		else
			++it;
	}
}

void SessionManager::purgeExpired() {
	const auto now = QDateTime::currentDateTimeUtc();
	for (auto it = sessions_.begin(); it != sessions_.end();) {
		if (it->expiresAt < now)
			it = sessions_.erase(it);
		else
			++it;
	}
}

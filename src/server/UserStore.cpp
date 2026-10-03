/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "UserStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTextStream>

namespace {
	constexpr auto kFileName = "online_users.json";
	constexpr auto kPlaintextCsvName = "online_users_passwords.csv";
	constexpr auto kPasswordAlphabet =
	    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789"; // ambiguous chars stripped

	// Tolerant single-line CSV parser: handles quoted fields and embedded commas.
	QStringList parseCsvLine(const QString &line) {
		QStringList out;
		QString cur;
		bool inQuote = false;
		for (int i = 0; i < line.size(); ++i) {
			const QChar c = line.at(i);
			if (inQuote) {
				if (c == '"') {
					if (i + 1 < line.size() && line.at(i + 1) == '"') {
						cur.append('"');
						++i;
					} else {
						inQuote = false;
					}
				} else {
					cur.append(c);
				}
			} else if (c == ',') {
				out.append(cur);
				cur.clear();
			} else if (c == '"' && cur.isEmpty()) {
				inQuote = true;
			} else {
				cur.append(c);
			}
		}
		out.append(cur);
		return out;
	}
} // namespace

UserStore::UserStore(QObject *parent) : QObject(parent) {}

bool UserStore::loadFromContestDir(const QString &contestDir) {
	users_.clear();
	QFile f(QDir(contestDir).filePath(kFileName));
	if (! f.exists()) {
		return true; // no file yet — empty store is valid
	}
	if (! f.open(QFile::ReadOnly)) {
		lastError_ = f.errorString();
		return false;
	}
	QJsonParseError err;
	const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
	if (err.error != QJsonParseError::NoError) {
		lastError_ = err.errorString();
		return false;
	}
	const auto obj = doc.object();
	const auto arr = obj.value("users").toArray();
	for (const auto &it : arr) {
		const auto u = it.toObject();
		User user;
		user.username = u.value("username").toString();
		user.displayName = u.value("displayName").toString();
		user.salt = QByteArray::fromBase64(u.value("salt").toString().toLatin1());
		user.passwordHash = QByteArray::fromBase64(u.value("hash").toString().toLatin1());
		user.iterations = u.value("iter").toInt(100000);
		user.plaintext = u.value("pw").toString();
		if (! user.username.isEmpty())
			users_.insert(user.username, user);
	}
	loadPlaintextFromCsv(contestDir);
	return true;
}

// Plaintext passwords are deliberately kept out of online_users.json, but the
// teacher still needs to re-export the account list after reopening a contest.
// Recover them from online_users_passwords.csv, the documented single place
// where plaintext is stored (written on batch generation).
void UserStore::loadPlaintextFromCsv(const QString &contestDir) {
	QFile f(QDir(contestDir).filePath(QLatin1String(kPlaintextCsvName)));
	if (! f.open(QFile::ReadOnly | QFile::Text))
		return;
	QTextStream ts(&f);
	ts.setEncoding(QStringConverter::Utf8);
	bool first = true;
	while (! ts.atEnd()) {
		QString line = ts.readLine();
		if (first) {
			first = false;
			if (line.startsWith(QChar(0xFEFF)))
				line.remove(0, 1); // strip UTF-8 BOM
			if (line.contains(QLatin1String("username")) && line.contains(QLatin1String("password")))
				continue; // header row
		}
		if (line.trimmed().isEmpty())
			continue;
		const auto fields = parseCsvLine(line);
		if (fields.size() < 3)
			continue;
		const auto it = users_.find(fields.at(0));
		if (it == users_.end())
			continue;
		if (it->plaintext.isEmpty())
			it->plaintext = fields.at(2);
	}
}

bool UserStore::saveToContestDir(const QString &contestDir) const {
	QJsonArray arr;
	for (const auto &u : users_) {
		QJsonObject obj;
		obj.insert("username", u.username);
		obj.insert("displayName", u.displayName);
		obj.insert("salt", QString::fromLatin1(u.salt.toBase64()));
		obj.insert("hash", QString::fromLatin1(u.passwordHash.toBase64()));
		obj.insert("iter", u.iterations);
		// Plaintext passwords are never persisted: they live in memory only (for
		// the CSV export) and in online_users_passwords.csv written on demand.
		arr.append(obj);
	}
	QJsonObject root;
	root.insert("version", 1);
	root.insert("users", arr);

	QSaveFile f(QDir(contestDir).filePath(kFileName));
	if (! f.open(QFile::WriteOnly)) {
		lastError_ = f.errorString();
		return false;
	}
	f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	if (! f.commit()) {
		lastError_ = f.errorString();
		return false;
	}
	return true;
}

bool UserStore::verify(const QString &username, const QString &password) const {
	const auto it = users_.constFind(username);
	if (it == users_.constEnd())
		return false;
	const auto h = pbkdf2(password, it->salt, it->iterations);
	if (h.size() != it->passwordHash.size())
		return false;
	// constant-time compare
	int diff = 0;
	for (int i = 0; i < h.size(); ++i)
		diff |= (h[i] ^ it->passwordHash[i]);
	return diff == 0;
}

bool UserStore::exists(const QString &username) const { return users_.contains(username); }

QString UserStore::displayNameOf(const QString &username) const {
	const auto it = users_.constFind(username);
	if (it == users_.constEnd())
		return {};
	return it->displayName.isEmpty() ? it->username : it->displayName;
}

QString UserStore::plaintextOf(const QString &username) const {
	const auto it = users_.constFind(username);
	if (it == users_.constEnd())
		return {};
	return it->plaintext;
}

QList<QString> UserStore::allUsernames() const { return users_.keys(); }

int UserStore::count() const { return users_.size(); }

QList<UserStore::GeneratedUser> UserStore::generateBatch(int count, const QString &prefix, int passwordLen) {
	QList<GeneratedUser> out;
	out.reserve(count);
	int idx = 1;
	for (int i = 0; i < count; ++i) {
		QString uname;
		do {
			uname = QStringLiteral("%1%2").arg(prefix).arg(idx, 2, 10, QChar('0'));
			++idx;
		} while (users_.contains(uname));
		const auto pwd = makePassword(passwordLen);
		addUser(uname, uname, pwd);
		GeneratedUser g{uname, uname, pwd};
		out.append(g);
	}
	return out;
}

bool UserStore::addUser(const QString &username, const QString &displayName,
                        const QString &plaintextPassword) {
	if (username.isEmpty())
		return false;
	User u;
	u.username = username;
	u.displayName = displayName.isEmpty() ? username : displayName;
	u.salt = makeSalt();
	u.iterations = 100000;
	u.passwordHash = pbkdf2(plaintextPassword, u.salt, u.iterations);
	u.plaintext = plaintextPassword;
	users_.insert(username, u);
	return true;
}

bool UserStore::removeUser(const QString &username) { return users_.remove(username) > 0; }

void UserStore::clear() { users_.clear(); }

QByteArray UserStore::makeSalt(int len) {
	QByteArray salt(len, Qt::Uninitialized);
	auto *gen = QRandomGenerator::system();
	for (int i = 0; i < len; ++i)
		salt[i] = static_cast<char>(gen->bounded(0, 256));
	return salt;
}

QString UserStore::makePassword(int len) {
	const QString alphabet = QString::fromLatin1(kPasswordAlphabet);
	QString out;
	out.reserve(len);
	auto *gen = QRandomGenerator::system();
	for (int i = 0; i < len; ++i)
		out.append(alphabet.at(gen->bounded(alphabet.size())));
	return out;
}

QByteArray UserStore::pbkdf2(const QString &password, const QByteArray &salt, int iterations) {
	return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(), salt, iterations,
	                                          32);
}

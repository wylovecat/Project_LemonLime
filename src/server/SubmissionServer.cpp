/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "SubmissionServer.h"

#include "SessionManager.h"
#include "UserStore.h"
#include "core/contest.h"
#include "core/task.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QTcpServer>
#include <QUrlQuery>

namespace {
constexpr auto kCookieName = "lemon_sid";
constexpr auto kStatementName = "statement.pdf";
constexpr auto kAuditLogName = "online_submissions.log";
constexpr auto kConfigName = "online_config.json";

QString readResource(const QString &path) {
	QFile f(path);
	if (!f.open(QFile::ReadOnly))
		return {};
	return QString::fromUtf8(f.readAll());
}

QByteArray readResourceBytes(const QString &path) {
	QFile f(path);
	if (!f.open(QFile::ReadOnly))
		return {};
	return f.readAll();
}

QString cookieValue(const QHttpServerRequest &req, const QString &name) {
	const auto headers = req.headers();
	const auto cookies = headers.values(QHttpHeaders::WellKnownHeader::Cookie);
	for (const auto &cookieHeader : cookies) {
		const auto parts = QString::fromUtf8(cookieHeader).split(';', Qt::SkipEmptyParts);
		for (const auto &p : parts) {
			const auto trimmed = p.trimmed();
			const int eq = trimmed.indexOf('=');
			if (eq <= 0)
				continue;
			if (trimmed.left(eq) == name)
				return trimmed.mid(eq + 1);
		}
	}
	return {};
}

QMap<QString, QString> parseFormUrlEncoded(const QByteArray &body) {
	QMap<QString, QString> out;
	const QUrlQuery q(QString::fromUtf8(body));
	for (const auto &kv : q.queryItems(QUrl::FullyDecoded))
		out.insert(kv.first, kv.second);
	return out;
}

QString extensionFor(const QString &lang) {
	const auto l = lang.toLower();
	if (l == "c") return QStringLiteral("c");
	if (l == "python" || l == "py") return QStringLiteral("py");
	if (l == "pascal" || l == "pas") return QStringLiteral("pas");
	return QStringLiteral("cpp");
}

QString encodeRfc5987(const QString &name) {
	const auto utf8 = name.toUtf8();
	QString out;
	for (char c : utf8) {
		const auto u = static_cast<unsigned char>(c);
		const bool unreserved =
		    (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u == '-' ||
		    u == '_' || u == '.' || u == '~';
		if (unreserved)
			out.append(QChar(c));
		else
			out.append(QString::asprintf("%%%02X", u));
	}
	return out;
}
} // namespace

SubmissionServer::SubmissionServer(QObject *parent) : QObject(parent) {
	userStore_ = new UserStore(this);
	sessions_ = new SessionManager(this);
}

SubmissionServer::~SubmissionServer() { stop(); }

void SubmissionServer::bindContest(Contest *contest, const QString &contestDir) {
	contest_ = contest;
	contestDir_ = contestDir;
	if (userStore_)
		userStore_->loadFromContestDir(contestDir);
	loadConfig();
}

void SubmissionServer::setContestWindow(bool enabled, const QDateTime &start,
                                        const QDateTime &end) {
	windowEnabled_ = enabled;
	startTime_ = start;
	endTime_ = end;
	saveConfig();
}

void SubmissionServer::setAutoJudge(bool on) {
	autoJudge_ = on;
	saveConfig();
}

bool SubmissionServer::loadConfig() {
	if (contestDir_.isEmpty())
		return false;
	QFile f(QDir(contestDir_).filePath(kConfigName));
	if (!f.exists())
		return true;
	if (!f.open(QFile::ReadOnly))
		return false;
	const auto obj = QJsonDocument::fromJson(f.readAll()).object();
	windowEnabled_ = obj.value("windowEnabled").toBool(false);
	startTime_ = QDateTime::fromString(obj.value("startTime").toString(), Qt::ISODate);
	endTime_ = QDateTime::fromString(obj.value("endTime").toString(), Qt::ISODate);
	autoJudge_ = obj.value("autoJudge").toBool(true);
	return true;
}

bool SubmissionServer::saveConfig() const {
	if (contestDir_.isEmpty())
		return false;
	QJsonObject obj;
	obj.insert("version", 1);
	obj.insert("windowEnabled", windowEnabled_);
	if (startTime_.isValid())
		obj.insert("startTime", startTime_.toString(Qt::ISODate));
	if (endTime_.isValid())
		obj.insert("endTime", endTime_.toString(Qt::ISODate));
	obj.insert("autoJudge", autoJudge_);
	QSaveFile f(QDir(contestDir_).filePath(kConfigName));
	if (!f.open(QFile::WriteOnly))
		return false;
	f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
	return f.commit();
}

bool SubmissionServer::start(const QHostAddress &addr, quint16 port, QString *errorOut) {
	if (running_)
		return true;
	if (!contest_ || contestDir_.isEmpty()) {
		if (errorOut)
			*errorOut = tr("No contest is currently bound to the server.");
		return false;
	}
	http_ = new QHttpServer(this);
	tcp_ = new QTcpServer(this);
	setupRoutes();
	if (!tcp_->listen(addr, port)) {
		if (errorOut)
			*errorOut = tcp_->errorString();
		delete http_;
		http_ = nullptr;
		delete tcp_;
		tcp_ = nullptr;
		return false;
	}
	if (!http_->bind(tcp_)) {
		if (errorOut)
			*errorOut = tr("Failed to bind HTTP server to TCP socket.");
		tcp_->close();
		delete http_;
		http_ = nullptr;
		delete tcp_;
		tcp_ = nullptr;
		return false;
	}
	boundAddress_ = addr;
	boundPort_ = tcp_->serverPort();
	running_ = true;
	emit started(boundAddress_, boundPort_);
	emit logMessage(tr("Server started on %1:%2").arg(boundAddress_.toString()).arg(boundPort_));
	return true;
}

void SubmissionServer::stop() {
	if (!running_)
		return;
	if (tcp_) {
		tcp_->close();
		tcp_->deleteLater();
		tcp_ = nullptr;
	}
	if (http_) {
		http_->deleteLater();
		http_ = nullptr;
	}
	running_ = false;
	emit stopped();
	emit logMessage(tr("Server stopped"));
}

void SubmissionServer::setupRoutes() {
	using namespace Qt::StringLiterals;

	http_->route("/", QHttpServerRequest::Method::Get,
	             [this](const QHttpServerRequest &req) { return handleIndex(req); });

	http_->route("/login", QHttpServerRequest::Method::Get,
	             [this]() { return handleLoginPage(); });
	http_->route("/login", QHttpServerRequest::Method::Post,
	             [this](const QHttpServerRequest &req) { return handleLoginPost(req); });
	http_->route("/logout", QHttpServerRequest::Method::Post,
	             [this](const QHttpServerRequest &req) { return handleLogout(req); });

	http_->route("/submit/<arg>", QHttpServerRequest::Method::Get,
	             [this](qint32 taskId, const QHttpServerRequest &req) {
		             return handleSubmitPage(taskId, req);
	             });

	http_->route("/api/tasks", QHttpServerRequest::Method::Get,
	             [this](const QHttpServerRequest &req) { return handleApiTasks(req); });

	http_->route("/api/submit/<arg>", QHttpServerRequest::Method::Post,
	             [this](qint32 taskId, const QHttpServerRequest &req) {
		             return handleApiSubmit(taskId, req);
	             });

	http_->route("/statement", QHttpServerRequest::Method::Get,
	             [this](const QHttpServerRequest &req) { return handleStatementPdf(req); });

	// static assets
	http_->route("/assets/css/app.css", QHttpServerRequest::Method::Get,
	             [this]() { return handleStatic(":/online/css/app.css", "text/css; charset=utf-8"); });
	http_->route("/assets/js/app.js", QHttpServerRequest::Method::Get,
	             [this]() {
		             return handleStatic(":/online/js/app.js", "application/javascript; charset=utf-8");
	             });
	http_->route("/assets/js/editor.js", QHttpServerRequest::Method::Get,
	             [this]() {
		             return handleStatic(":/online/js/editor.js",
		                                 "application/javascript; charset=utf-8");
	             });
}

QString SubmissionServer::sessionUser(const QHttpServerRequest &req) const {
	const auto sid = cookieValue(req, kCookieName);
	if (sid.isEmpty() || !sessions_)
		return {};
	QString user;
	return sessions_->validate(sid, &user) ? user : QString{};
}

bool SubmissionServer::requireSession(const QHttpServerRequest &req, QString *user) const {
	const auto u = sessionUser(req);
	if (u.isEmpty())
		return false;
	if (user)
		*user = u;
	return true;
}

QHttpServerResponse SubmissionServer::redirect(const QString &location, const QString &setCookie) const {
	QHttpServerResponse resp(QHttpServerResponder::StatusCode::SeeOther);
	QHttpHeaders h;
	h.append(QHttpHeaders::WellKnownHeader::Location, location);
	if (!setCookie.isEmpty())
		h.append(QHttpHeaders::WellKnownHeader::SetCookie, setCookie);
	h.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
	resp.setHeaders(h);
	return resp;
}

QHttpServerResponse SubmissionServer::jsonError(int httpStatus, const QString &message) const {
	QJsonObject obj{{"error", message}};
	QHttpServerResponse resp("application/json", QJsonDocument(obj).toJson(QJsonDocument::Compact),
	                         static_cast<QHttpServerResponder::StatusCode>(httpStatus));
	return resp;
}

QHttpServerResponse SubmissionServer::handleStatic(const QString &resourcePath, const QString &contentType) {
	const auto bytes = readResourceBytes(resourcePath);
	if (bytes.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
	QHttpServerResponse resp(contentType.toLatin1(), bytes);
	QHttpHeaders h = resp.headers();
	h.append(QHttpHeaders::WellKnownHeader::CacheControl, "private, max-age=300");
	resp.setHeaders(h);
	return resp;
}

QHttpServerResponse SubmissionServer::handleLoginPage() {
	auto html = readResource(":/online/login.html");
	if (html.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::InternalServerError);
	return QHttpServerResponse("text/html; charset=utf-8", html.toUtf8());
}

QHttpServerResponse SubmissionServer::handleLoginPost(const QHttpServerRequest &req) {
	const auto form = parseFormUrlEncoded(req.body());
	const auto username = form.value("username").trimmed();
	const auto password = form.value("password");
	if (username.isEmpty() || password.isEmpty())
		return redirect("/login?err=1");
	if (!userStore_ || !userStore_->verify(username, password))
		return redirect("/login?err=1");
	const auto token = sessions_->createSession(username);
	const auto cookie =
	    QStringLiteral("%1=%2; Path=/; HttpOnly; SameSite=Lax").arg(kCookieName, token);
	emit logMessage(tr("Login OK: %1").arg(username));
	return redirect("/", cookie);
}

QHttpServerResponse SubmissionServer::handleLogout(const QHttpServerRequest &req) {
	const auto sid = cookieValue(req, kCookieName);
	if (!sid.isEmpty())
		sessions_->destroy(sid);
	const auto cookie =
	    QStringLiteral("%1=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0").arg(kCookieName);
	return redirect("/login", cookie);
}

QHttpServerResponse SubmissionServer::handleIndex(const QHttpServerRequest &req) {
	QString user;
	if (!requireSession(req, &user))
		return redirect("/login");
	auto html = readResource(":/online/index.html");
	if (html.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::InternalServerError);
	return QHttpServerResponse("text/html; charset=utf-8", html.toUtf8());
}

QHttpServerResponse SubmissionServer::handleSubmitPage(qint32 taskId, const QHttpServerRequest &req) {
	QString user;
	if (!requireSession(req, &user))
		return redirect("/login");
	if (!contest_ || taskId < 0 || taskId >= contest_->getTaskList().size())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
	auto html = readResource(":/online/submit.html");
	if (html.isEmpty())
		return QHttpServerResponse(QHttpServerResponder::StatusCode::InternalServerError);
	return QHttpServerResponse("text/html; charset=utf-8", html.toUtf8());
}

QHttpServerResponse SubmissionServer::handleApiTasks(const QHttpServerRequest &req) {
	QString user;
	if (!requireSession(req, &user))
		return jsonError(401, tr("Not authenticated"));
	if (!contest_)
		return jsonError(500, tr("No contest bound"));

	const auto taskList = contest_->getTaskList();
	QJsonArray arr;
	for (int i = 0; i < taskList.size(); ++i) {
		const auto *t = taskList.at(i);
		QJsonObject obj;
		obj.insert("id", i);
		obj.insert("title", t->getProblemTitle());
		obj.insert("totalScore", t->getTotalScore());
		obj.insert("timeLimitMs", t->getTotalTimeLimit());
		obj.insert("sourceFileName", t->getSourceFileName());

		// determine last submission time across known extensions
		const auto srcDir = QDir(QDir(contestDir_).filePath(QStringLiteral("source/%1").arg(user)));
		const auto base = t->getSourceFileName();
		const QString folder = t->getSubFolderCheck() ? base + QChar('/') : QString();
		QDateTime latest;
		for (const auto *ext : {"cpp", "c", "py", "pas"}) {
			const QFileInfo fi(srcDir, folder + base + QChar('.') + QString::fromLatin1(ext));
			if (fi.exists() && (!latest.isValid() || fi.lastModified() > latest))
				latest = fi.lastModified();
		}
		obj.insert("submittedAt", latest.isValid() ? latest.toString(Qt::ISODate) : QString());
		arr.append(obj);
	}

	QJsonObject root;
	root.insert("contestTitle", contest_->getContestTitle());
	root.insert("user", user);
	root.insert("displayName",
	            userStore_ ? userStore_->displayNameOf(user) : QString());
	root.insert("hasStatement", QFile::exists(QDir(contestDir_).filePath(kStatementName)));
	root.insert("tasks", arr);
	root.insert("serverNow", QDateTime::currentDateTime().toString(Qt::ISODate));
	root.insert("windowEnabled", windowEnabled_);
	if (windowEnabled_ && startTime_.isValid())
		root.insert("startTime", startTime_.toString(Qt::ISODate));
	if (windowEnabled_ && endTime_.isValid())
		root.insert("endTime", endTime_.toString(Qt::ISODate));
	return QHttpServerResponse("application/json",
	                           QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QHttpServerResponse SubmissionServer::handleApiSubmit(qint32 taskId, const QHttpServerRequest &req) {
	QString user;
	if (!requireSession(req, &user))
		return jsonError(401, tr("Not authenticated"));
	if (!contest_)
		return jsonError(500, tr("No contest bound"));
	if (taskId < 0 || taskId >= contest_->getTaskList().size())
		return jsonError(404, tr("Unknown task"));

	if (windowEnabled_) {
		const auto now = QDateTime::currentDateTime();
		if (startTime_.isValid() && now < startTime_)
			return jsonError(403, tr("比赛尚未开始"));
		if (endTime_.isValid() && now > endTime_)
			return jsonError(403, tr("比赛已结束"));
	}

	const auto body = req.body();
	if (body.isEmpty())
		return jsonError(400, tr("Empty body"));
	if (body.size() > maxSourceBytes_)
		return jsonError(413,
		                 tr("Source too large (max %1 KB)").arg(maxSourceBytes_ / 1024));

	const auto contentType = QString::fromUtf8(
	    req.headers().combinedValue(QHttpHeaders::WellKnownHeader::ContentType));

	QByteArray source;
	QString language = QStringLiteral("cpp");
	if (contentType.contains("application/json", Qt::CaseInsensitive)) {
		QJsonParseError err;
		const auto doc = QJsonDocument::fromJson(body, &err);
		if (err.error != QJsonParseError::NoError)
			return jsonError(400, tr("Bad JSON: %1").arg(err.errorString()));
		const auto obj = doc.object();
		source = obj.value("source").toString().toUtf8();
		const auto lang = obj.value("language").toString();
		if (!lang.isEmpty())
			language = lang;
	} else {
		source = body;
	}
	if (source.trimmed().isEmpty())
		return jsonError(400, tr("Empty source"));

	QString writeErr;
	if (!writeSubmission(user, taskId, source, extensionFor(language), &writeErr))
		return jsonError(500, writeErr);

	const auto sha =
	    QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
	appendAuditLog(user, taskId, source.size(), sha);
	emit submissionReceived(user, contest_->getTaskList().at(taskId)->getProblemTitle(),
	                        source.size());

	bool willJudge = false;
	if (autoJudge_ && contest_) {
		willJudge = true;
		auto contestPtr = contest_.data();
		const QString u = user;
		const int t = taskId;
		// Run on main thread on the next event loop tick so the HTTP response goes
		// out first; then refresh selectors and trigger judge.
		QMetaObject::invokeMethod(
		    contestPtr,
		    [contestPtr, u, t]() {
			    contestPtr->refreshContestantList();
			    QList<std::pair<QString, QVector<int>>> work;
			    work.append({u, QVector<int>{t}});
			    contestPtr->judge(work);
		    },
		    Qt::QueuedConnection);
	}

	QJsonObject ok{
	    {"ok", true},
	    {"size", source.size()},
	    {"submittedAt", QDateTime::currentDateTime().toString(Qt::ISODate)},
	    {"willJudge", willJudge},
	};
	return QHttpServerResponse("application/json", QJsonDocument(ok).toJson(QJsonDocument::Compact));
}

QHttpServerResponse SubmissionServer::handleStatementPdf(const QHttpServerRequest &req) {
	QString user;
	if (!requireSession(req, &user))
		return redirect("/login");
	const auto path = QDir(contestDir_).filePath(kStatementName);
	QFile f(path);
	if (!f.exists() || !f.open(QFile::ReadOnly))
		return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
	const auto bytes = f.readAll();
	QHttpServerResponse resp("application/pdf", bytes);
	QHttpHeaders h = resp.headers();
	h.append(QHttpHeaders::WellKnownHeader::ContentDisposition,
	         QStringLiteral("inline; filename=\"statement.pdf\"; filename*=UTF-8''%1")
	             .arg(encodeRfc5987(kStatementName)));
	h.append(QHttpHeaders::WellKnownHeader::CacheControl, "private, max-age=60");
	resp.setHeaders(h);
	return resp;
}

bool SubmissionServer::writeSubmission(const QString &username, int taskIndex,
                                       const QByteArray &source, const QString &extension,
                                       QString *errOut) {
	if (!contest_)
		return false;
	const auto *task = contest_->getTaskList().at(taskIndex);
	const auto base = task->getSourceFileName();
	if (base.isEmpty()) {
		if (errOut)
			*errOut = tr("Task %1 has empty source file name").arg(taskIndex);
		return false;
	}

	// directory layout: <contestDir>/source/<username>/[<base>/]<base>.<ext>
	const QString sourceRoot =
	    QDir(contestDir_).filePath(QStringLiteral("source/%1").arg(username));
	QDir().mkpath(sourceRoot);

	QString outFile;
	if (task->getSubFolderCheck()) {
		const QString sub = QDir(sourceRoot).filePath(base);
		QDir().mkpath(sub);
		outFile = QDir(sub).filePath(base + QStringLiteral(".") + extension);
	} else {
		outFile = QDir(sourceRoot).filePath(base + QStringLiteral(".") + extension);
	}

	QSaveFile f(outFile);
	if (!f.open(QFile::WriteOnly | QFile::Truncate)) {
		if (errOut)
			*errOut = f.errorString();
		return false;
	}
	if (f.write(source) != source.size()) {
		if (errOut)
			*errOut = f.errorString();
		return false;
	}
	if (!f.commit()) {
		if (errOut)
			*errOut = f.errorString();
		return false;
	}
	return true;
}

void SubmissionServer::appendAuditLog(const QString &username, int taskIndex, qint64 bytes,
                                      const QString &sha256) {
	QFile f(QDir(contestDir_).filePath(kAuditLogName));
	if (!f.open(QFile::Append | QFile::Text))
		return;
	const auto line =
	    QStringLiteral("%1\t%2\ttask=%3\tbytes=%4\tsha256=%5\n")
	        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
	        .arg(username)
	        .arg(taskIndex)
	        .arg(bytes)
	        .arg(sha256);
	f.write(line.toUtf8());
}

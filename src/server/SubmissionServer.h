/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime Online
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QString>

class QHttpServer;
class QTcpServer;
class Contest;
class UserStore;
class SessionManager;
class QHttpServerRequest;
class QHttpServerResponse;

class SubmissionServer : public QObject {
	Q_OBJECT
  public:
	explicit SubmissionServer(QObject *parent = nullptr);
	~SubmissionServer() override;

	void bindContest(Contest *contest, const QString &contestDir);
	UserStore *userStore() const { return userStore_.data(); }
	QString contestDir() const { return contestDir_; }

	bool start(const QHostAddress &addr, quint16 port, QString *errorOut = nullptr);
	void stop();
	bool isRunning() const { return running_; }
	quint16 port() const { return boundPort_; }
	QHostAddress address() const { return boundAddress_; }

	void setMaxSourceBytes(int n) { maxSourceBytes_ = n; }

	bool windowEnabled() const { return windowEnabled_; }
	QDateTime startTime() const { return startTime_; }
	QDateTime endTime() const { return endTime_; }
	void setContestWindow(bool enabled, const QDateTime &start, const QDateTime &end);

	bool autoJudge() const { return autoJudge_; }
	void setAutoJudge(bool on);

	bool loadConfig();
	bool saveConfig() const;

  signals:
	void started(QHostAddress addr, quint16 port);
	void stopped();
	void submissionReceived(QString username, QString taskName, int bytes);
	void logMessage(QString msg);

  private:
	void setupRoutes();
	QHttpServerResponse handleStatic(const QString &resourcePath, const QString &contentType);
	QHttpServerResponse handleLoginPage();
	QHttpServerResponse handleLoginPost(const QHttpServerRequest &req);
	QHttpServerResponse handleLogout(const QHttpServerRequest &req);
	QHttpServerResponse handleIndex(const QHttpServerRequest &req);
	QHttpServerResponse handleSubmitPage(qint32 taskId, const QHttpServerRequest &req);
	QHttpServerResponse handleApiTasks(const QHttpServerRequest &req);
	QHttpServerResponse handleApiSubmit(qint32 taskId, const QHttpServerRequest &req);
	QHttpServerResponse handleStatementPdf(const QHttpServerRequest &req);

	QString sessionUser(const QHttpServerRequest &req) const;
	bool requireSession(const QHttpServerRequest &req, QString *user) const;
	QHttpServerResponse redirect(const QString &location, const QString &setCookie = {}) const;
	QHttpServerResponse jsonError(int httpStatus, const QString &message) const;

	bool writeSubmission(const QString &username, int taskIndex, const QByteArray &source,
	                     const QString &extension, QString *errOut);
	void appendAuditLog(const QString &username, int taskIndex, qint64 bytes, const QString &sha256);

	QHttpServer *http_ = nullptr;
	QTcpServer *tcp_ = nullptr;
	QPointer<Contest> contest_;
	QString contestDir_;
	QPointer<UserStore> userStore_;
	QPointer<SessionManager> sessions_;
	QHostAddress boundAddress_;
	quint16 boundPort_ = 0;
	bool running_ = false;
	int maxSourceBytes_ = 64 * 1024;

	bool windowEnabled_ = false;
	QDateTime startTime_;
	QDateTime endTime_;

	bool autoJudge_ = true;
};

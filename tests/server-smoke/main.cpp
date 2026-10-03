/*
 * SPDX-FileCopyrightText: 2026 Project LemonLime
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "base/settings.h"
#include "core/contest.h"
#include "server/SubmissionServer.h"
#include "server/UserStore.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

#define LEMON_MODULE_NAME "ServerSmoke"

// ---------------------------------------------------------------------------
// End-to-end smoke test for the embedded online submission service:
// start SubmissionServer over real HTTP, log in, submit, and assert what
// lands in the contest directory. Pins two contracts:
//   1. online_users.json never contains plaintext passwords
//   2. submissions are written as <sourceFileName>.<language extension>
//      (so the task's source file name must not carry an extension)
// ---------------------------------------------------------------------------
class TestServerSmoke : public QObject {
	Q_OBJECT
	QTemporaryDir contestDir_;
	Settings *settings_{};
	Contest *contest_{};
	SubmissionServer *server_{};
	quint16 port_{};
	QString cookie_;
	QString password1_;
	QString password2_;

	struct HttpResult {
		int status{};
		QByteArray body;
		QByteArray location;
		QByteArray setCookie;
	};

	HttpResult http(const QString &method, const QString &path, const QByteArray &contentType = {},
	                const QByteArray &body = {}, const QString &cookie = {}) {
		QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port_).arg(path)));
		req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		const auto &activeCookie = cookie.isEmpty() ? cookie_ : cookie;
		if (! activeCookie.isEmpty())
			req.setRawHeader("Cookie", activeCookie.toUtf8());
		if (! contentType.isEmpty())
			req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
		// A fresh manager per request: QNetworkAccessManager transparently
		// re-sends cookies from earlier Set-Cookie responses, which would
		// defeat the explicit no-session assertions below.
		QNetworkAccessManager localNam;
		QNetworkReply *reply = nullptr;
		if (method == QStringLiteral("POST"))
			reply = localNam.post(req, body);
		else if (method == QStringLiteral("GET"))
			reply = localNam.get(req);
		else {
			qWarning() << "unsupported method" << method;
			return {};
		}
		QEventLoop loop;
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		loop.exec();
		HttpResult result;
		result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		result.body = reply->readAll();
		result.location = reply->rawHeader("Location");
		result.setCookie = reply->rawHeader("Set-Cookie");
		reply->deleteLater();
		return result;
	}

	QString loginAndGetCookie(const QString &user, const QString &password) {
		const auto body = QStringLiteral("username=%1&password=%2").arg(user, password).toUtf8();
		const auto r =
		    http(QStringLiteral("POST"), QStringLiteral("/login"), "application/x-www-form-urlencoded", body);
		if (r.status != 303)
			return {};
		// Set-Cookie: lemon_sid=<token>; Path=/; HttpOnly; SameSite=Lax
		const auto setCookie = QString::fromUtf8(r.setCookie);
		return setCookie.section(QLatin1Char(';'), 0, 0);
	}

  private slots:
	void initTestCase();
	void cleanupTestCase();

	void testUsersFileHasNoPlaintext();
	void testPlaintextRecoveredFromCsvAfterReload();
	void testLoginRejectsBadPassword();
	void testSubmitWritesSourceFile();
	void testResubmissionOverwrites();
	void testAuditLogWritten();
	void testAutoJudgeSurvivesRapidSubmissions();
};

void TestServerSmoke::initTestCase() {
	QVERIFY(contestDir_.isValid());
	const QString dir = contestDir_.path();

	// A contest with one traditional task. NOTE: sourceFileName carries no
	// extension - the server appends ".<language>" when writing, and the
	// desktop judge matches "<sourceFileName>.<compiler extension>".
	const QByteArray cdf =
	    R"({"version":"1.0","contestTitle":"Smoke Contest","contestants":[],"tasks":[)"
	    R"({"problemTitle":"A+B Problem","sourceFileName":"answer","inputFileName":"input",)"
	    R"("outputFileName":"output","standardInputCheck":true,"standardOutputCheck":true,)"
	    R"("taskType":0,"subFolderCheck":false,"comparisonMode":0,"diffArguments":"",)"
	    R"("realPrecision":4,"specialJudge":"","testCases":[]}]})";
	QSaveFile cdfFile(dir + QStringLiteral("/test.cdf"));
	QVERIFY(cdfFile.open(QFile::WriteOnly));
	cdfFile.write(cdf);
	QVERIFY(cdfFile.commit());

	// refreshContestantList() scans Settings::sourcePath(), which is relative
	// to the current working directory - run from inside the contest dir.
	QVERIFY(QDir::setCurrent(dir));
	settings_ = new Settings();
	contest_ = new Contest(this);
	contest_->setSettings(settings_);
	QFile in(dir + QStringLiteral("/test.cdf"));
	QVERIFY(in.open(QFile::ReadOnly));
	QVERIFY(contest_->readFromJson(QJsonDocument::fromJson(in.readAll()).object()) == 0);

	server_ = new SubmissionServer(this);
	server_->bindContest(contest_, dir);
	server_->setAutoJudge(false);

	const auto batch = server_->userStore()->generateBatch(2, QStringLiteral("stu"), 8);
	QVERIFY(server_->userStore()->saveToContestDir(dir));
	QCOMPARE(batch.size(), 2);
	password1_ = batch.first().plaintextPassword;
	password2_ = batch.last().plaintextPassword;

	QString error;
	QVERIFY(server_->start(QHostAddress::LocalHost, 0, &error));
	port_ = server_->port();
	QVERIFY(port_ > 0);

	cookie_ = loginAndGetCookie(batch.first().username, password1_);
	QVERIFY(cookie_.startsWith(QStringLiteral("lemon_sid=")));
}

void TestServerSmoke::cleanupTestCase() {
	if (server_)
		server_->stop();
}

void TestServerSmoke::testUsersFileHasNoPlaintext() {
	QFile f(contestDir_.filePath(QStringLiteral("online_users.json")));
	QVERIFY(f.open(QFile::ReadOnly));
	const auto doc = QJsonDocument::fromJson(f.readAll());
	const auto users = doc.object().value(QStringLiteral("users")).toArray();
	QCOMPARE(users.size(), 2);
	for (const auto &u : users)
		QVERIFY2(! u.toObject().contains(QStringLiteral("pw")),
		         "online_users.json must not persist plaintext passwords");
	QVERIFY(server_->userStore()->verify(QStringLiteral("stu01"), password1_));
	QVERIFY(! server_->userStore()->verify(QStringLiteral("stu01"), QStringLiteral("wrong")));
}

// Reopening a contest must still let the teacher export the account list with
// real passwords. Plaintext is absent from online_users.json, so it has to be
// recovered from online_users_passwords.csv (the documented single plaintext
// store, written on batch generation).
void TestServerSmoke::testPlaintextRecoveredFromCsvAfterReload() {
	QFile csv(contestDir_.filePath(QStringLiteral("online_users_passwords.csv")));
	QVERIFY(csv.open(QFile::WriteOnly | QFile::Text));
	{
		QTextStream ts(&csv);
		ts.setEncoding(QStringConverter::Utf8);
		ts << "username,display_name,password\n";
		ts << "stu01,stu01," << password1_ << "\n";
		ts << "stu02,stu02," << password2_ << "\n";
	}
	csv.close();

	UserStore reloaded;
	QVERIFY(reloaded.loadFromContestDir(contestDir_.path()));
	QCOMPARE(reloaded.plaintextOf(QStringLiteral("stu01")), password1_);
	QCOMPARE(reloaded.plaintextOf(QStringLiteral("stu02")), password2_);
	// the recovered plaintext must still verify against the hashed store
	QVERIFY(reloaded.verify(QStringLiteral("stu01"), password1_));

	// and a CSV entry for an unknown user must be ignored, not resurrected
	QVERIFY(reloaded.allUsernames().size() == 2);
}

void TestServerSmoke::testLoginRejectsBadPassword() {
	const auto body = QStringLiteral("username=stu01&password=definitely-wrong").toUtf8();
	const auto r =
	    http(QStringLiteral("POST"), QStringLiteral("/login"), "application/x-www-form-urlencoded", body);
	QCOMPARE(r.status, 303);
	QVERIFY(QUrl(QString::fromUtf8(r.location)).path() == QStringLiteral("/login"));
	// and a request without a session must not see the task list
	const auto savedCookie = cookie_;
	cookie_.clear();
	const auto page = http(QStringLiteral("GET"), QStringLiteral("/"));
	cookie_ = savedCookie;
	QCOMPARE(page.status, 303);
	QVERIFY(QUrl(QString::fromUtf8(page.location)).path() == QStringLiteral("/login"));
}

void TestServerSmoke::testSubmitWritesSourceFile() {
	const QByteArray code = "#include <iostream>\nint main(){return 0;}\n";
	const auto body = QJsonDocument(QJsonObject{{QStringLiteral("source"), QString::fromUtf8(code)},
	                                            {QStringLiteral("language"), QStringLiteral("cpp")}})
	                      .toJson(QJsonDocument::Compact);
	const auto r = http(QStringLiteral("POST"), QStringLiteral("/api/submit/0"), "application/json", body);
	QCOMPARE(r.status, 200);
	const auto reply = QJsonDocument::fromJson(r.body).object();
	QCOMPARE(reply.value(QStringLiteral("ok")).toBool(), true);

	const QString path = contestDir_.filePath(QStringLiteral("source/stu01/answer.cpp"));
	QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("missing %1").arg(path)));
	QFile f(path);
	QVERIFY(f.open(QFile::ReadOnly));
	QCOMPARE(f.readAll(), code);
}

void TestServerSmoke::testResubmissionOverwrites() {
	const QByteArray second = "int main(){return 1;}\n";
	const auto body = QJsonDocument(QJsonObject{{QStringLiteral("source"), QString::fromUtf8(second)},
	                                            {QStringLiteral("language"), QStringLiteral("cpp")}})
	                      .toJson(QJsonDocument::Compact);
	const auto r = http(QStringLiteral("POST"), QStringLiteral("/api/submit/0"), "application/json", body);
	QCOMPARE(r.status, 200);

	QFile f(contestDir_.filePath(QStringLiteral("source/stu01/answer.cpp")));
	QVERIFY(f.open(QFile::ReadOnly));
	QCOMPARE(f.readAll(), second);

	const auto view = http(QStringLiteral("GET"), QStringLiteral("/api/source/0"));
	QCOMPARE(view.status, 200);
	const auto obj = QJsonDocument::fromJson(view.body).object();
	QCOMPARE(obj.value(QStringLiteral("content")).toString(), QString::fromUtf8(second));
	QCOMPARE(obj.value(QStringLiteral("filename")).toString(), QStringLiteral("answer.cpp"));
}

void TestServerSmoke::testAuditLogWritten() {
	QFile f(contestDir_.filePath(QStringLiteral("online_submissions.log")));
	QVERIFY(f.open(QFile::ReadOnly));
	const auto lines = f.readAll().split('\n');
	int nonEmpty = 0;
	for (const auto &line : lines)
		if (! line.trimmed().isEmpty())
			++nonEmpty;
	QVERIFY(nonEmpty >= 2);
}

void TestServerSmoke::testAutoJudgeSurvivesRapidSubmissions() {
	server_->setAutoJudge(true);
	const auto submit = [this](const char *code, const QString &cookie) {
		const auto body = QJsonDocument(QJsonObject{{QStringLiteral("source"), QLatin1String(code)},
		                                            {QStringLiteral("language"), QStringLiteral("cpp")}})
		                      .toJson(QJsonDocument::Compact);
		return http(QStringLiteral("POST"), QStringLiteral("/api/submit/0"), "application/json", body,
		            cookie);
	};
	const auto cookie2 = loginAndGetCookie(QStringLiteral("stu02"), password2_);
	QVERIFY(cookie2.startsWith(QStringLiteral("lemon_sid=")));
	// 第二个请求会在第一个评测批次运行期间（嵌套事件循环内）被服务端处理，
	// 由此触发 Contest::judge 的重入路径 —— 修复前此处必然崩溃
	const auto first = submit("int main(){return 10;}", cookie_);
	const auto second = submit("int main(){return 20;}", cookie2);
	QCOMPARE(first.status, 200);
	QCOMPARE(second.status, 200);
	QCOMPARE(QJsonDocument::fromJson(first.body).object().value(QStringLiteral("willJudge")).toBool(), true);
	QCOMPARE(QJsonDocument::fromJson(second.body).object().value(QStringLiteral("willJudge")).toBool(), true);
	// both submissions must have been processed and the server must still be up
	QVERIFY(QFile::exists(contestDir_.filePath(QStringLiteral("source/stu01/answer.cpp"))));
	QVERIFY(QFile::exists(contestDir_.filePath(QStringLiteral("source/stu02/answer.cpp"))));
	QVERIFY(server_->isRunning());
	server_->setAutoJudge(false);
}

QTEST_GUILESS_MAIN(TestServerSmoke)
#include "main.moc"

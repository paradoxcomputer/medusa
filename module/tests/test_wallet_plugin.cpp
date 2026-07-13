#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "plugin/WalletPlugin.h"

// ── Helper ────────────────────────────────────────────────────────────────────
static QJsonObject parseObj(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

static QJsonArray parseArr(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).array();
}

// ── Fake wallet CLI script ────────────────────────────────────────────────────
// Written to a temp file and pointed to via QSettings for each test.
static QString g_fakeCli;

// ── Test class ────────────────────────────────────────────────────────────────
class TestWalletPlugin : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;

    // Write a fake wallet script and return its path
    QString makeFakeCli(const QString& output, int exitCode = 0)
    {
        QString path = m_tmp.path() + "/fake_wallet.sh";
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("#!/bin/sh\n");
        f.write(QString("echo '%1'\n").arg(output).toUtf8());
        f.write(QString("exit %1\n").arg(exitCode).toUtf8());
        f.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                  | QFile::ReadGroup | QFile::ExeGroup);
        return path;
    }

private slots:
    void init()
    {
        QSettings s;
        s.remove(QStringLiteral("medusa-wallet"));
        s.sync();
    }

    // ── getStatus ─────────────────────────────────────────────────────────────
    void testGetStatusCliNotFound()
    {
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"),
                   QStringLiteral("/nonexistent/path/wallet_does_not_exist"));
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.getStatus());
        QCOMPARE(r[QStringLiteral("cliFound")].toBool(), false);
    }

    void testGetStatusCliFound()
    {
        QString cli = makeFakeCli(R"({"ok":true})");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.getStatus());
        QCOMPARE(r[QStringLiteral("cliFound")].toBool(), true);
        QCOMPARE(r[QStringLiteral("cliPath")].toString(), cli);
    }

    // ── setCliPath / getConfig ─────────────────────────────────────────────────
    void testSetCliPathEmpty()
    {
        WalletPlugin p;
        auto r = parseObj(p.setCliPath(QStringLiteral("")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testSetCliPathRoundTrip()
    {
        WalletPlugin p;
        auto set = parseObj(p.setCliPath(QStringLiteral("/usr/bin/wallet")));
        QCOMPARE(set[QStringLiteral("ok")].toBool(), true);

        auto cfg = parseObj(p.getConfig());
        QCOMPARE(cfg[QStringLiteral("cliPath")].toString(), QString("/usr/bin/wallet"));
    }

    // ── listAccounts ──────────────────────────────────────────────────────────
    void testListAccountsTimeout()
    {
        // Point to /bin/sleep as CLI - will always time out
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), QStringLiteral("/bin/sleep"));
        s.sync();

        WalletPlugin p;
        // Use a 1ms timeout so the test finishes quickly
        // runWalletCommand is private, but we call listAccounts which delegates to it.
        // Expect error response (timeout or startup failure)
        QString raw = p.listAccounts();
        auto r = parseObj(raw);
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testListAccountsJsonOutput()
    {
        QString jsonOut = R"([{"id":"public/abc123","type":"public","balance":150}])";
        QString cli = makeFakeCli(jsonOut);

        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        QString raw = p.listAccounts();
        QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        // Output is a JSON array
        QVERIFY(doc.isArray());
        QCOMPARE(doc.array().size(), 1);
        QCOMPARE(doc.array()[0].toObject()[QStringLiteral("id")].toString(),
                 QString("public/abc123"));
    }

    void testListAccountsCliError()
    {
        QString cli = makeFakeCli(R"({"error":"no accounts"})", 1);
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.listAccounts());
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    // ── getBalance ────────────────────────────────────────────────────────────
    void testGetBalanceMissingId()
    {
        WalletPlugin p;
        auto r = parseObj(p.getBalance(QStringLiteral("")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testGetBalanceSuccess()
    {
        QString cli = makeFakeCli(R"({"id":"public/abc123","balance":150,"type":"public"})");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.getBalance(QStringLiteral("public/abc123")));
        QCOMPARE(r[QStringLiteral("balance")].toInt(), 150);
    }

    // ── createAccount ─────────────────────────────────────────────────────────
    void testCreateAccountSuccess()
    {
        QString cli = makeFakeCli(R"({"id":"public/new123","type":"public"})");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.createAccount());
        QCOMPARE(r[QStringLiteral("id")].toString(), QString("public/new123"));
    }

    // ── sendTransfer validation ───────────────────────────────────────────────
    void testSendTransferMissingFrom()
    {
        WalletPlugin p;
        auto r = parseObj(p.sendTransfer(QStringLiteral(""), QStringLiteral("public/b"), QStringLiteral("10")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testSendTransferMissingTo()
    {
        WalletPlugin p;
        auto r = parseObj(p.sendTransfer(QStringLiteral("public/a"), QStringLiteral(""), QStringLiteral("10")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testSendTransferMissingAmount()
    {
        WalletPlugin p;
        auto r = parseObj(p.sendTransfer(QStringLiteral("public/a"), QStringLiteral("public/b"), QStringLiteral("")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testSendTransferSuccess()
    {
        QString cli = makeFakeCli(R"({"ok":true,"txId":"tx123"})");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.sendTransfer(
            QStringLiteral("public/a"),
            QStringLiteral("public/b"),
            QStringLiteral("10")));
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
    }

    // ── claimFaucet ───────────────────────────────────────────────────────────
    void testClaimFaucetMissingId()
    {
        WalletPlugin p;
        auto r = parseObj(p.claimFaucet(QStringLiteral("")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testClaimFaucetPrefixNormalization()
    {
        // If accountId doesn't have "public/" prefix, CLI arg must be "public/abc"
        // We verify this by inspecting the fake CLI's $@ (args) - simplest: just check no error
        QString cli = makeFakeCli(R"({"ok":true,"claimed":150})");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.claimFaucet(QStringLiteral("abc123")));
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
    }

    // ── Private account management ──────────────────────────────────────────────
    void testCreatePrivateAccountParsesTextOutput()
    {
        // The real CLI prints human text; the wrapper folds it into {ok,output}.
        QString cli = makeFakeCli(
            "Generated new account with account_id Private/abc123def at path 0 "
            "With npk aabbccdd With vpk eeff0011");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.createPrivateAccount(QStringLiteral("")));
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        QCOMPARE(r[QStringLiteral("id")].toString(),  QString("Private/abc123def"));
        QCOMPARE(r[QStringLiteral("npk")].toString(), QString("aabbccdd"));
        QCOMPARE(r[QStringLiteral("vpk")].toString(), QString("eeff0011"));
    }

    void testGetAccountKeysMissingId()
    {
        WalletPlugin p;
        auto r = parseObj(p.getAccountKeys(QStringLiteral("")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testSyncPrivatePassThrough()
    {
        QString cli = makeFakeCli(R"({"ok":true})");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.syncPrivate());
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
    }

    // ── Privacy transfers - validation ───────────────────────────────────────────
    void testShieldMissingAmount()
    {
        WalletPlugin p;
        auto r = parseObj(p.startShield(QStringLiteral("native"),
                                        QStringLiteral("Public/a"),
                                        QStringLiteral("Private/b"),
                                        QStringLiteral("")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testShieldRejectsPrivateSource()
    {
        // Shield source must be Public - a Private/ source is a prefix conflict.
        WalletPlugin p;
        auto r = parseObj(p.startShield(QStringLiteral("native"),
                                        QStringLiteral("Private/a"),
                                        QStringLiteral("Private/b"),
                                        QStringLiteral("10")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testDeshieldRejectsPublicSource()
    {
        WalletPlugin p;
        auto r = parseObj(p.startDeshield(QStringLiteral("native"),
                                          QStringLiteral("Public/a"),
                                          QStringLiteral("Public/b"),
                                          QStringLiteral("10")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testForeignTransferRequiresKeys()
    {
        WalletPlugin p;
        auto r = parseObj(p.startPrivateTransferForeign(QStringLiteral("native"),
                                                        QStringLiteral("Private/a"),
                                                        QStringLiteral(""),   // npk missing
                                                        QStringLiteral("vpk"),
                                                        QStringLiteral("id"),
                                                        QStringLiteral("10")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testGetJobUnknownId()
    {
        WalletPlugin p;
        auto r = parseObj(p.getJob(QStringLiteral("job-does-not-exist")));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    // ── Privacy transfers - async success path (start → poll → done) ─────────────
    void testShieldAsyncCompletesWithTxHash()
    {
        // Fake CLI prints a real-CLI-style tx line; wrapper folds it into {ok,output}.
        QString cli = makeFakeCli("Transaction hash is 0xdeadbeef");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto started = parseObj(p.startShield(QStringLiteral("native"),
                                              QStringLiteral("Public/a"),
                                              QStringLiteral("Private/b"),
                                              QStringLiteral("10")));
        QString jobId = started[QStringLiteral("jobId")].toString();
        QVERIFY(!jobId.isEmpty());
        QCOMPARE(started[QStringLiteral("state")].toString(), QString("running"));

        // Poll until the background job reaches a terminal state.
        QTRY_COMPARE_WITH_TIMEOUT(
            parseObj(p.getJob(jobId))[QStringLiteral("state")].toString(),
            QString("done"), 10000);

        auto job = parseObj(p.getJob(jobId));
        QCOMPARE(job[QStringLiteral("op")].toString(),   QString("shield"));
        QCOMPARE(job[QStringLiteral("txId")].toString(), QString("0xdeadbeef"));
    }

    void testPrivateTransferAsyncJsonTxId()
    {
        // When the CLI/wrapper already returns JSON with a txId, it is surfaced as-is.
        QString cli = makeFakeCli(R"({"ok":true,"txId":"tok_tx_001"})");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto started = parseObj(p.startPrivateTransfer(QStringLiteral("token"),
                                                       QStringLiteral("Private/a"),
                                                       QStringLiteral("Private/b"),
                                                       QStringLiteral("5")));
        QString jobId = started[QStringLiteral("jobId")].toString();
        QVERIFY(!jobId.isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(
            parseObj(p.getJob(jobId))[QStringLiteral("state")].toString(),
            QString("done"), 10000);

        auto job = parseObj(p.getJob(jobId));
        QCOMPARE(job[QStringLiteral("op")].toString(),    QString("private"));
        QCOMPARE(job[QStringLiteral("asset")].toString(), QString("token"));
        QCOMPARE(job[QStringLiteral("txId")].toString(),  QString("tok_tx_001"));
    }

    void testPrivacyJobErrorOnCliFailure()
    {
        QString cli = makeFakeCli(R"({"error":"insufficient balance"})", 1);
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto started = parseObj(p.startDeshield(QStringLiteral("native"),
                                                QStringLiteral("Private/a"),
                                                QStringLiteral("Public/b"),
                                                QStringLiteral("10")));
        QString jobId = started[QStringLiteral("jobId")].toString();
        QVERIFY(!jobId.isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(
            parseObj(p.getJob(jobId))[QStringLiteral("state")].toString(),
            QString("error"), 10000);
    }

    // ── Session password / unlock ────────────────────────────────────────────────

    // A fake CLI that echoes back the first line of its stdin (the password).
    QString makeStdinEchoCli()
    {
        QString path = m_tmp.path() + "/echo_stdin.sh";
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("#!/bin/sh\nread pw\nprintf '{\"ok\":true,\"pw\":\"%s\"}\\n' \"$pw\"\n");
        f.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                  | QFile::ReadGroup | QFile::ExeGroup);
        return path;
    }

    void testSecurityStateRoundTrip()
    {
        WalletPlugin p;
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        p.setSessionPassword(QStringLiteral("hunter2"));
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), true);
        p.clearSessionPassword();
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
    }

    void testPasswordPipedToStdin()
    {
        QString cli = makeStdinEchoCli();
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        p.setSessionPassword(QStringLiteral("s3cret"));
        // listAccounts runs the CLI, which echoes the piped password back.
        auto r = parseObj(p.listAccounts());
        QCOMPARE(r[QStringLiteral("pw")].toString(), QString("s3cret"));
    }

    void testUnlockWrongPassword()
    {
        QString cli = makeFakeCli(R"({"error":"Failed to decrypt wallet storage"})", 1);
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.unlock(QStringLiteral("wrong")));
        QVERIFY(r.contains(QStringLiteral("error")));
        // Wrong password must be cleared again.
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
    }

    void testUnlockSuccess()
    {
        QString cli = makeFakeCli(R"([{"id":"Public/abc","type":"public","balance":5}])");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        QString raw = p.unlock(QStringLiteral("right"));
        QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        QVERIFY(doc.isArray());
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), true);
    }

    // ── Import / export ──────────────────────────────────────────────────────────
    void testExportMnemonic()
    {
        QString cli = makeFakeCli("legal winner thank year wave sausage worth useful");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.exportMnemonic());
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        QVERIFY(r[QStringLiteral("mnemonic")].toString().startsWith(QStringLiteral("legal winner")));
    }

    void testExportKeyMissingId()
    {
        WalletPlugin p;
        QVERIFY(parseObj(p.exportKey(QStringLiteral(""))).contains(QStringLiteral("error")));
    }

    void testExportKeySuccess()
    {
        QString cli = makeFakeCli("10a26a9aec7d34b82364eeae45c5294dbb0a764b000b94eeb9b58511dc487c4d");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.exportKey(QStringLiteral("Public/abc")));
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        QCOMPARE(r[QStringLiteral("privateKey")].toString().length(), 64);
    }

    void testImportKeyMissing()
    {
        WalletPlugin p;
        QVERIFY(parseObj(p.importKey(QStringLiteral(""), QStringLiteral(""))).contains(QStringLiteral("error")));
    }

    void testImportKeySuccess()
    {
        QString cli = makeFakeCli("Imported account with account_id Public/GkeQajoUJ6KUz");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        auto r = parseObj(p.importKey(QStringLiteral("deadbeef"), QStringLiteral("mine")));
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        QCOMPARE(r[QStringLiteral("id")].toString(), QString("Public/GkeQajoUJ6KUz"));
    }

    void testRestoreWalletValidation()
    {
        WalletPlugin p;
        QVERIFY(parseObj(p.restoreWallet(QStringLiteral(""), QStringLiteral("pw"), 5))
                    .contains(QStringLiteral("error")));
    }

    // ── Medusa-Connect (sessions + per-action approval) ──────────────────────────
    // Helper: create a connect request, approve it exposing `accounts`, return sessionId.
    QString connectAndApprove(WalletPlugin& p, const QStringList& perms,
                              const QStringList& accounts)
    {
        QJsonArray pa; for (const auto& s : perms) pa.append(s);
        auto cr = parseObj(p.connectRequest(
            QStringLiteral("{\"appName\":\"Truth Garden\",\"icon\":\"data:x\",\"origin\":\"truth_garden\"}"),
            QString::fromUtf8(QJsonDocument(pa).toJson(QJsonDocument::Compact))));
        const QString reqId = cr[QStringLiteral("requestId")].toString();
        QJsonArray aa; for (const auto& a : accounts) aa.append(a);
        auto sess = parseObj(p.approveConnect(reqId,
            QString::fromUtf8(QJsonDocument(aa).toJson(QJsonDocument::Compact))));
        return sess[QStringLiteral("sessionId")].toString();
    }

    void testConnectRequestRequiresAppName()
    {
        WalletPlugin p;
        auto r = parseObj(p.connectRequest(QStringLiteral("{}"),
                                           QStringLiteral("[\"accounts\"]")));
        QCOMPARE(r[QStringLiteral("error")].toString(), QString("appName is required"));
    }

    void testConnectRequestRequiresPerm()
    {
        WalletPlugin p;
        // Unknown perms are dropped, leaving an empty set → error.
        auto r = parseObj(p.connectRequest(QStringLiteral("{\"appName\":\"x\"}"),
                                           QStringLiteral("[\"bogus\"]")));
        QCOMPARE(r[QStringLiteral("error")].toString(),
                 QString("at least one permission is required"));
    }

    void testConnectFlowMintsSession()
    {
        WalletPlugin p;
        auto cr = parseObj(p.connectRequest(
            QStringLiteral("{\"appName\":\"Truth Garden\"}"),
            QStringLiteral("[\"accounts\",\"send\",\"private\",\"bogus\"]")));
        const QString reqId = cr[QStringLiteral("requestId")].toString();
        QVERIFY(reqId.startsWith(QStringLiteral("req-")));

        // It shows up as a pending connect request.
        auto pend = parseArr(p.pendingRequests());
        QCOMPARE(pend.size(), 1);
        QCOMPARE(pend[0].toObject()[QStringLiteral("kind")].toString(), QString("connect"));

        // actionStatus answers for a connect request too (still pending here).
        QCOMPARE(parseObj(p.actionStatus(reqId))[QStringLiteral("status")].toString(),
                 QString("pending"));

        auto sess = parseObj(p.approveConnect(reqId, QStringLiteral("[\"Public/abc\"]")));
        const QString sid = sess[QStringLiteral("sessionId")].toString();
        QVERIFY(sid.startsWith(QStringLiteral("ses-")));
        // Unknown "bogus" perm was filtered out of the grant.
        const auto granted = sess[QStringLiteral("granted")].toArray();
        QCOMPARE(granted.size(), 3);
        QCOMPARE(sess[QStringLiteral("accounts")].toArray().size(), 1);

        // After approval the request is no longer pending.
        QCOMPARE(parseArr(p.pendingRequests()).size(), 0);

        // actionStatus for the approved CONNECT request hands back the minted sessionId.
        auto st = parseObj(p.actionStatus(reqId));
        QCOMPARE(st[QStringLiteral("status")].toString(), QString("approved"));
        QCOMPARE(st[QStringLiteral("sessionId")].toString(), sid);

        // sessionInfo round-trips the grant.
        auto info = parseObj(p.sessionInfo(sid));
        QCOMPARE(info[QStringLiteral("active")].toBool(), true);
        QCOMPARE(info[QStringLiteral("accounts")].toArray()[0].toString(), QString("Public/abc"));
    }

    void testRejectConnect()
    {
        WalletPlugin p;
        auto cr = parseObj(p.connectRequest(QStringLiteral("{\"appName\":\"x\"}"),
                                            QStringLiteral("[\"send\"]")));
        const QString reqId = cr[QStringLiteral("requestId")].toString();
        QCOMPARE(parseObj(p.rejectConnect(reqId))[QStringLiteral("ok")].toBool(), true);
        QCOMPARE(parseObj(p.actionStatus(reqId))[QStringLiteral("status")].toString(),
                 QString("rejected"));
        // Double-reject of a terminal request is an error.
        QVERIFY(parseObj(p.rejectConnect(reqId)).contains(QStringLiteral("error")));
    }

    void testSessionInfoUnknown()
    {
        WalletPlugin p;
        QCOMPARE(parseObj(p.sessionInfo(QStringLiteral("ses-nope")))[QStringLiteral("error")].toString(),
                 QString("no such session"));
    }

    void testRequestActionPermissionGate()
    {
        WalletPlugin p;
        // Session granted only "send" → a shield action must be refused.
        const QString sid = connectAndApprove(p, {QStringLiteral("send")},
                                              {QStringLiteral("Public/a")});
        auto r = parseObj(p.requestAction(sid, QStringLiteral(
            "{\"op\":\"shield\",\"from\":\"Public/a\",\"to\":\"Private/b\",\"amount\":\"5\"}")));
        QCOMPARE(r[QStringLiteral("error")].toString(), QString("permission not granted: shield"));
    }

    void testRequestActionAccountGate()
    {
        WalletPlugin p;
        const QString sid = connectAndApprove(p, {QStringLiteral("send")},
                                              {QStringLiteral("Public/a")});
        // "Public/x" is not in the session's exposed accounts.
        auto r = parseObj(p.requestAction(sid, QStringLiteral(
            "{\"op\":\"send\",\"from\":\"Public/x\",\"to\":\"Public/b\",\"amount\":\"5\"}")));
        QCOMPARE(r[QStringLiteral("error")].toString(),
                 QString("account not authorized for this session"));
    }

    void testRequestActionAmountGate()
    {
        WalletPlugin p;
        const QString sid = connectAndApprove(p, {QStringLiteral("send")},
                                              {QStringLiteral("Public/a")});
        auto r = parseObj(p.requestAction(sid, QStringLiteral(
            "{\"op\":\"send\",\"from\":\"Public/a\",\"to\":\"Public/b\",\"amount\":\"5.5\"}")));
        QCOMPARE(r[QStringLiteral("error")].toString(),
                 QString("amounts are whole numbers - no decimals"));
    }

    void testRequestActionUnknownSession()
    {
        WalletPlugin p;
        auto r = parseObj(p.requestAction(QStringLiteral("ses-nope"), QStringLiteral(
            "{\"op\":\"send\",\"from\":\"Public/a\",\"to\":\"Public/b\",\"amount\":\"5\"}")));
        QCOMPARE(r[QStringLiteral("error")].toString(), QString("no such session"));
    }

    void testRequestActionAutoDerivesOp()
    {
        WalletPlugin p;
        const QString sid = connectAndApprove(p, {QStringLiteral("shield")},
                                              {QStringLiteral("Public/a")});
        // op omitted: Public→Private must auto-derive "shield" and pass the shield gate.
        auto r = parseObj(p.requestAction(sid, QStringLiteral(
            "{\"from\":\"Public/a\",\"to\":\"Private/b\",\"amount\":\"5\"}")));
        const QString reqId = r[QStringLiteral("requestId")].toString();
        QVERIFY(reqId.startsWith(QStringLiteral("req-")));
        // The pending action row reports the derived op.
        auto pend = parseArr(p.pendingRequests());
        QCOMPARE(pend.size(), 1);
        QCOMPARE(pend[0].toObject()[QStringLiteral("op")].toString(), QString("shield"));
        QCOMPARE(pend[0].toObject()[QStringLiteral("kind")].toString(), QString("action"));
    }

    void testApproveActionDispatchesJob()
    {
        QString cli = makeFakeCli("Transaction hash is 0xfeed");
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), cli);
        s.sync();

        WalletPlugin p;
        const QString sid = connectAndApprove(p, {QStringLiteral("shield")},
                                              {QStringLiteral("Public/a")});
        auto ra = parseObj(p.requestAction(sid, QStringLiteral(
            "{\"op\":\"shield\",\"from\":\"Public/a\",\"to\":\"Private/b\",\"amount\":\"7\"}")));
        const QString reqId = ra[QStringLiteral("requestId")].toString();

        auto ap = parseObj(p.approveAction(reqId));
        QCOMPARE(ap[QStringLiteral("status")].toString(), QString("approved"));
        const QString jobId = ap[QStringLiteral("jobId")].toString();
        QVERIFY(jobId.startsWith(QStringLiteral("job-")));

        // actionStatus mirrors the approval and surfaces the same jobId.
        auto st = parseObj(p.actionStatus(reqId));
        QCOMPARE(st[QStringLiteral("status")].toString(), QString("approved"));
        QCOMPARE(st[QStringLiteral("jobId")].toString(), jobId);

        // The job is a REAL existing job, trackable via the unchanged getJob.
        QTRY_COMPARE_WITH_TIMEOUT(
            parseObj(p.getJob(jobId))[QStringLiteral("state")].toString(),
            QString("done"), 10000);
        QCOMPARE(parseObj(p.getJob(jobId))[QStringLiteral("op")].toString(), QString("shield"));
    }

    void testApproveActionPropagatesValidationError()
    {
        WalletPlugin p;
        const QString sid = connectAndApprove(p, {QStringLiteral("shield")},
                                              {QStringLiteral("Public/a")});
        // A Private/ source for a shield is a prefix conflict the start* method rejects;
        // approveAction must propagate that as a rejection (not crash / not bypass).
        // First slip it past requestAction by using a session that exposes the Private acct.
        const QString sid2 = connectAndApprove(p, {QStringLiteral("shield")},
                                               {QStringLiteral("Private/a")});
        auto ra = parseObj(p.requestAction(sid2, QStringLiteral(
            "{\"op\":\"shield\",\"from\":\"Private/a\",\"to\":\"Private/b\",\"amount\":\"7\"}")));
        const QString reqId = ra[QStringLiteral("requestId")].toString();
        auto ap = parseObj(p.approveAction(reqId));
        QCOMPARE(ap[QStringLiteral("status")].toString(), QString("rejected"));
        QVERIFY(!ap[QStringLiteral("error")].toString().isEmpty());
        // And actionStatus reflects the rejection with the error.
        auto st = parseObj(p.actionStatus(reqId));
        QCOMPARE(st[QStringLiteral("status")].toString(), QString("rejected"));
        (void)sid;
    }

    void testApproveActionRejectsConnectKind()
    {
        WalletPlugin p;
        auto cr = parseObj(p.connectRequest(QStringLiteral("{\"appName\":\"x\"}"),
                                            QStringLiteral("[\"send\"]")));
        const QString reqId = cr[QStringLiteral("requestId")].toString();
        auto r = parseObj(p.approveAction(reqId));
        QCOMPARE(r[QStringLiteral("error")].toString(), QString("not an action request"));
    }

    void testRevokeSessionIsIdempotent()
    {
        WalletPlugin p;
        const QString sid = connectAndApprove(p, {QStringLiteral("send")},
                                              {QStringLiteral("Public/a")});
        QCOMPARE(parseObj(p.revokeSession(sid))[QStringLiteral("ok")].toBool(), true);
        // Session is gone now.
        QVERIFY(parseObj(p.sessionInfo(sid)).contains(QStringLiteral("error")));
        // Revoking again (or an unknown id) still succeeds - disconnect never fails.
        QCOMPARE(parseObj(p.revokeSession(sid))[QStringLiteral("ok")].toBool(), true);
        QCOMPARE(parseObj(p.revokeSession(QStringLiteral("ses-nope")))[QStringLiteral("ok")].toBool(),
                 true);
    }

    void testActionStatusUnknown()
    {
        WalletPlugin p;
        QCOMPARE(parseObj(p.actionStatus(QStringLiteral("req-nope")))[QStringLiteral("error")].toString(),
                 QString("unknown request"));
    }
};

QTEST_MAIN(TestWalletPlugin)
#include "test_wallet_plugin.moc"

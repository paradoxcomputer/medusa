#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QMetaMethod>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QProcess>

#include <functional>

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

// The first of `candidates` that exists and is executable, "" if none does. Deliberately the
// same root-owned directories resolveSystemBin() trusts, so a test that needs a real system
// helper picks the same one the module would. Free functions, not private slots: QTest invokes
// every zero-argument private slot as a test case.
static QString firstExecutable(std::initializer_list<const char*> candidates)
{
    for (const char* c : candidates) {
        const QString p = QString::fromLatin1(c);
        if (QFileInfo(p).isExecutable())
            return p;
    }
    return QString();
}

// The interpreter the shipped CLI's `#!/usr/bin/env python3` shebang resolves to.
static QString trustedPython3()
{
    return firstExecutable({ "/usr/bin/python3", "/bin/python3", "/usr/local/bin/python3",
                             "/run/current-system/sw/bin/python3",
                             "/nix/var/nix/profiles/default/bin/python3",
                             "/opt/homebrew/bin/python3" });
}

// `env`, by absolute path, for the shell stand-in that dumps its own environment. Resolving it
// here rather than letting /bin/sh find it keeps the test honest on a box whose $PATH does not
// contain /usr/bin (the Nix build shell).
static QString trustedEnvBin()
{
    return firstExecutable({ "/usr/bin/env", "/bin/env", "/run/current-system/sw/bin/env",
                             "/usr/local/bin/env" });
}

// ── Fake wallet CLI script ────────────────────────────────────────────────────
// Written to a temp file and pointed to via MEDUSA_WALLET_CLI for each test. It cannot go
// through setCliPath/QSettings any more: a stored override is now validated (existing,
// executable, named wallet*, in a known bin dir) on write AND on read, and a temp-dir script
// deliberately fails that. The env override exists for exactly this - the process environment
// belongs to whoever launched the module, not to a co-resident caller.
static QString g_fakeCli;

// The session password every gated test uses.
static const QString kPw = QStringLiteral("correct horse");

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

    // Point the plugin at a fake CLI for the duration of one test.
    static void useCli(const QString& path) { qputenv("MEDUSA_WALLET_CLI", path.toUtf8()); }
    // …and at a fake sequencer. The ONLY override for the sequencer binary is this environment
    // variable: medusa-wallet/seqPath in QSettings is no longer read on the execution path.
    static void useSeq(const QString& path) { qputenv("MEDUSA_SEQ_PATH", path.toUtf8()); }

    // A fake CLI that behaves like an ENCRYPTED store: it reads the password from stdin line 1
    // and fails to decrypt unless it is `pw`. Without this the suite could not tell an honest
    // unlock from a minted one, because every other fake CLI succeeds whatever it is handed.
    QString makePasswordCheckingCli(const QString& pw, const QString& output = QStringLiteral("[]"))
    {
        const QString path = m_tmp.path() + QStringLiteral("/pwcheck_wallet.sh");
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("#!/bin/sh\nread got\n");
        f.write(QString("if [ \"$got\" != '%1' ]; then\n"
                        "  echo '{\"error\":\"Failed to decrypt wallet storage\"}'\n"
                        "  exit 1\n"
                        "fi\n").arg(pw).toUtf8());
        f.write(QString("echo '%1'\n").arg(output).toUtf8());
        f.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

    // A fake CLI that behaves like a real one on a wallet it CREATES: it writes an encrypted
    // store where the real CLI would, then echoes `output`. Several verbs now verify the store
    // they just caused to be written (createEncryptedWallet, encryptPlaintextWallet, a proven
    // restoreWallet), so a stand-in that writes nothing is no longer faithful - and "the CLI
    // reported success but wrote no wallet" is itself one of the failures being checked for.
    QString makeStoreWritingCli(const QString& output)
    {
        const QString home    = qEnvironmentVariable("LEE_WALLET_HOME_DIR");
        const QString storage = home + QStringLiteral("/storage.json");
        const QString path    = m_tmp.path() + QStringLiteral("/storewriting_wallet.sh");
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("#!/bin/sh\nread pw\n");
        f.write(QString("mkdir -p '%1'\n").arg(home).toUtf8());
        f.write(QString("printf '{\"kdf\":\"argon2id\",\"ct\":\"sealed\"}' > '%1'\n")
                    .arg(storage).toUtf8());
        f.write(QString("echo '%1'\n").arg(output).toUtf8());
        f.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

    // A fake CLI that performs the real migration: any write seals the store with the password
    // it was handed on stdin, so a later call with the same password opens it and a later call
    // with a different one does not - which is what makes the post-seal verification meaningful.
    QString makeSealingCli(const QString& storage)
    {
        const QString path = m_tmp.path() + QStringLiteral("/migrate_wallet.sh");
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("#!/bin/sh\nread pw\n");
        f.write(QString("if [ -f '%1' ] && grep -q '\"kdf\"' '%1'; then\n"
                        "  grep -q \"sealed-with-$pw\\\"\" '%1' || {\n"
                        "    echo '{\"error\":\"Failed to decrypt wallet storage\"}'; exit 1; }\n"
                        "fi\n").arg(storage).toUtf8());
        f.write(QString("printf '{\"kdf\":\"argon2id\",\"ct\":\"sealed-with-%s\"}' \"$pw\" "
                        "> '%1'\n").arg(storage).toUtf8());
        f.write("echo 'Generated new account with account_id Public/fresh at path 0'\n");
        f.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

    // A fake CLI that records its argv and its stdin, so a test can prove where a secret
    // travelled. $1.. are written one per line to <path>.argv, stdin verbatim to <path>.stdin.
    QString makeRecordingCli(const QString& output)
    {
        const QString path = m_tmp.path() + QStringLiteral("/record_wallet.sh");
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("#!/bin/sh\n");
        f.write(QString(": > '%1.argv'\nfor a in \"$@\"; do echo \"$a\" >> '%1.argv'; done\n")
                    .arg(path).toUtf8());
        f.write(QString("cat > '%1.stdin'\n").arg(path).toUtf8());
        f.write(QString("echo '%1'\n").arg(output).toUtf8());
        f.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                  | QFile::ReadGroup | QFile::ExeGroup);
        return path;
    }

    static QString slurp(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

    // Establish a session the way a user does: unlock(), which VERIFIES the candidate against the
    // store by running the CLI, and refuses a wrong one.
    //
    // This used to be `p.setSessionPassword(kPw)`, and that made every gate test worthless: the
    // setter skipped its own conditional gate whenever m_password was empty, so a caller could
    // lock the wallet (ungated, by design) and then install a password it chose. The suite was
    // arming its sessions through the exact bypass it was meant to be pinning. There is no such
    // verb any more, and unlock() is the only path left - the same one the UI uses.
    //
    // The CLI swap is local to the unlock: the caller's own fake CLI (with the canned output the
    // verb under test needs) is put back before returning.
    //
    // It also puts an ENCRYPTED store on disk first, because unlock() now refuses when there is
    // no store at all. That refusal is a real fix, not a formality: `account list` on an empty
    // wallet home makes the CLI CREATE a wallet sealed with whatever password it is handed, so
    // unlock("anything") on a fresh install used to succeed and hand out a session for a wallet
    // it had just minted. Modelling "a session implies a store" is also simply more honest.
    void arm(WalletPlugin& p)
    {
        if (!QFile::exists(qEnvironmentVariable("LEE_WALLET_HOME_DIR")
                           + QStringLiteral("/storage.json")))
            writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"armed\"}"));
        const QByteArray prev = qgetenv("MEDUSA_WALLET_CLI");
        useCli(makePasswordCheckingCli(kPw));
        const QString r = p.unlock(kPw);
        QVERIFY2(!parseObj(r).contains(QStringLiteral("error")), qPrintable(r));
        if (prev.isEmpty()) qunsetenv("MEDUSA_WALLET_CLI");
        else                qputenv("MEDUSA_WALLET_CLI", prev);
    }

    // An encrypted store on disk, for the tests that drive unlock() directly rather than arm().
    QString armStore()
    {
        return writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"armed\"}"));
    }

    using GatedCall = std::function<QString(WalletPlugin&, const QString&)>;

    // Every verb the gate covers unconditionally. Deleting a gate from any one of them makes
    // testGatedVerbsRefuseWrongPassword fail on that row by name.
    QVector<QPair<QString, GatedCall>> gatedVerbs()
    {
        return {
            { QStringLiteral("exportMnemonic"), [](WalletPlugin& p, const QString& pw) {
                return p.exportMnemonic(pw); } },
            { QStringLiteral("exportKey"), [](WalletPlugin& p, const QString& pw) {
                return p.exportKey(QStringLiteral("Public/a"), pw); } },
            { QStringLiteral("sendTransfer"), [](WalletPlugin& p, const QString& pw) {
                return p.sendTransfer(QStringLiteral("Public/a"), QStringLiteral("Public/b"),
                                      QStringLiteral("1"), pw); } },
            { QStringLiteral("startSendTransfer"), [](WalletPlugin& p, const QString& pw) {
                return p.startSendTransfer(QStringLiteral("Public/a"), QStringLiteral("Public/c"),
                                           QStringLiteral("1"), pw); } },
            { QStringLiteral("startSendToken"), [](WalletPlugin& p, const QString& pw) {
                return p.startSendToken(QStringLiteral("Public/a"), QStringLiteral("Public/d"),
                                        QStringLiteral("def1"), QStringLiteral("1"), pw); } },
            { QStringLiteral("startShield"), [](WalletPlugin& p, const QString& pw) {
                return p.startShield(QStringLiteral("native"), QStringLiteral("Public/a"),
                                     QStringLiteral("Private/s1"), QStringLiteral("1"),
                                     QString(), pw); } },
            { QStringLiteral("startDeshield"), [](WalletPlugin& p, const QString& pw) {
                return p.startDeshield(QStringLiteral("native"), QStringLiteral("Private/a"),
                                       QStringLiteral("Public/e"), QStringLiteral("1"),
                                       QString(), pw); } },
            { QStringLiteral("startPrivateTransfer"), [](WalletPlugin& p, const QString& pw) {
                return p.startPrivateTransfer(QStringLiteral("native"), QStringLiteral("Private/a"),
                                              QStringLiteral("Private/s2"), QStringLiteral("1"), pw); } },
            { QStringLiteral("startPrivateTransferForeign"), [](WalletPlugin& p, const QString& pw) {
                return p.startPrivateTransferForeign(QStringLiteral("native"),
                                                     QStringLiteral("Private/a"),
                                                     QStringLiteral("npk"), QStringLiteral("vpk"),
                                                     QStringLiteral("ident"), QStringLiteral("1"), pw); } },
            { QStringLiteral("consolidateToken"), [](WalletPlugin& p, const QString& pw) {
                return p.consolidateToken(QStringLiteral("Public/a"), QStringLiteral("def1"), pw); } },
            { QStringLiteral("approveAction"), [](WalletPlugin& p, const QString& pw) {
                return p.approveAction(QStringLiteral("req-1"), pw); } },
            // approveZone is approveAction's twin: it repoints the wallet at a sequencer a
            // foreign app named. It was the one no-user-interaction chain left after the first
            // round of gating.
            { QStringLiteral("approveZone"), [](WalletPlugin& p, const QString& pw) {
                return p.approveZone(QStringLiteral("req-1"), pw); } },
        };
    }

private slots:
    void initTestCase()
    {
        // Isolate every test from the developer's real wallet home (~/.local/share/
        // medusa-wallet-home): the sequencer-status tests read wallet_config.json and
        // write a sequencer log under walletHome(), and must never touch real state.
        qputenv("LEE_WALLET_HOME_DIR", (m_tmp.path() + QStringLiteral("/home")).toUtf8());
        // With no MEDUSA_WALLET_CLI set, cliPath() resolves <module>/bin/wallet -> ~/.local/bin/
        // wallet -> PATH, and for this binary moduleBinDir() is <test binary dir>/bin. Put a
        // harmless script there so a test that forgets to set a fake CLI can never reach the
        // developer's real wallet binary.
        makeAllowedCli();
    }

    void init()
    {
        QSettings s;
        s.remove(QStringLiteral("medusa-wallet"));
        s.sync();
        qunsetenv("MEDUSA_WALLET_CLI");
        qunsetenv("MEDUSA_SEQ_PATH");
        qunsetenv("MEDUSA_IDLE_LOCK_MS");
        // The gate now reads the store on disk (a plaintext one cannot be protected by a
        // password, so it refuses), which makes leftover storage.json state from a previous test
        // able to change another test's verdict. Start every test with an empty wallet home.
        const QString home = qEnvironmentVariable("LEE_WALLET_HOME_DIR");
        QDir d(home);
        const QStringList stale = d.entryList({QStringLiteral("storage.json*")}, QDir::Files);
        for (const QString& f : stale)
            QFile::remove(d.filePath(f));
    }

    // ── getStatus ─────────────────────────────────────────────────────────────
    void testGetStatusCliNotFound()
    {
        useCli(QStringLiteral("/nonexistent/path/wallet_does_not_exist"));

        WalletPlugin p;
        auto r = parseObj(p.getStatus());
        QCOMPARE(r[QStringLiteral("cliFound")].toBool(), false);
    }

    void testGetStatusCliFound()
    {
        QString cli = makeFakeCli(R"({"ok":true})");
        useCli(cli);

        WalletPlugin p;
        auto r = parseObj(p.getStatus());
        QCOMPARE(r[QStringLiteral("cliFound")].toBool(), true);
        QCOMPARE(r[QStringLiteral("cliPath")].toString(), cli);
    }

    // ── setCliPath / getConfig ─────────────────────────────────────────────────
    // A path inside the module's own bin dir - the one location setCliPath accepts that a test
    // can create. moduleBinDir() is dladdr of a symbol in this binary, so for the test
    // executable it is <the test binary's dir>/bin.
    QString makeAllowedCli()
    {
        const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/bin");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/wallet");
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("#!/bin/sh\necho '{\"ok\":true}'\n");
        f.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

    // Plant a file that satisfies EVERY rule the old path filter applied - an existing,
    // executable, `wallet`-prefixed file directly inside the module's own bin dir - but that is
    // not the bundled binary. If a filter were still load-bearing, this is what would get past
    // it. It records the fact that it ran, and its stdin, to <path>.ran / <path>.stdin.
    QString plantAllowlistedPoison()
    {
        const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/bin");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/wallet-evil");
        QFile::remove(path + QStringLiteral(".ran"));
        QFile::remove(path + QStringLiteral(".stdin"));
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("#!/bin/sh\n");
        f.write(QString("echo ran > '%1.ran'\ncat > '%1.stdin'\necho '[]'\n").arg(path).toUtf8());
        f.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

    // The setter is gone. It was ranked above seed export as a finding: the stored path was
    // executed in preference to the bundled binary AND handed the session password on stdin, so
    // it was code execution plus password capture that outlived both a reboot and the module that
    // planted it. A password gate could not hold it (see the next test), so nothing is stored.
    void testSetCliPathIsRefusedEvenWithTheRightPassword()
    {
        const QString allowed = makeAllowedCli();     // the most plausible path there is
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.setCliPath(allowed, kPw));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("not-supported"));
        // Nothing was written, so nothing is there to be executed later.
        auto cfg = parseObj(p.getConfig());
        QVERIFY(cfg[QStringLiteral("cliPath")].toString().isEmpty());
        QCOMPARE(cfg[QStringLiteral("cliPathConfigurable")].toBool(), false);
    }

    // Review finding 2(b), the route that needed no IPC and no password at all: QSettings for
    // this plugin is a plain user-writable INI, so a co-resident module writes
    // medusa-wallet/cliPath into it directly, and being the same uid it can also drop a
    // `wallet*` binary into an allowlisted directory to satisfy the read-side filter. Both
    // halves are reproduced here. The override is no longer consulted, so neither matters.
    void testPoisonedCliPathSettingIsNeverExecuted()
    {
        const QString evil = plantAllowlistedPoison();
        QSettings s;                                    // written directly, as the attacker does
        s.setValue(QStringLiteral("medusa-wallet/cliPath"), evil);
        s.sync();

        // The one remaining override is the process environment, which a co-resident module
        // cannot write. It must win, and the poison must never run.
        const QString real = makeRecordingCli(QStringLiteral("[]"));
        useCli(real);
        armStore();
        WalletPlugin p;
        p.unlock(QStringLiteral("the-users-real-password"));
        p.listAccounts();

        QVERIFY2(!QFile::exists(evil + QStringLiteral(".ran")), "the poisoned CLI was executed");
        QVERIFY2(!QFile::exists(evil + QStringLiteral(".stdin")),
                 "the poisoned CLI was handed the session password");
        auto cfg = parseObj(p.getConfig());
        QCOMPARE(cfg[QStringLiteral("cliPathEff")].toString(), real);
        QCOMPARE(cfg[QStringLiteral("cliPath")].toString(), evil);        // still visible…
        QCOMPARE(cfg[QStringLiteral("cliPathIgnored")].toBool(), true);   // …and always disowned

        // …and with no environment override either, the effective binary is still not the poison.
        qunsetenv("MEDUSA_WALLET_CLI");
        WalletPlugin p2;
        QVERIFY(parseObj(p2.getConfig())[QStringLiteral("cliPathEff")].toString() != evil);
    }

    // ── listAccounts ──────────────────────────────────────────────────────────
    void testListAccountsTimeout()
    {
        // Point to /bin/sleep as CLI - will always time out
        useCli(QStringLiteral("/bin/sleep"));

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

        useCli(cli);

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
        useCli(cli);

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
        useCli(cli);

        WalletPlugin p;
        auto r = parseObj(p.getBalance(QStringLiteral("public/abc123")));
        QCOMPARE(r[QStringLiteral("balance")].toInt(), 150);
    }

    // ── createAccount ─────────────────────────────────────────────────────────
    void testCreateAccountSuccess()
    {
        QString cli = makeFakeCli(R"({"id":"public/new123","type":"public"})");
        useCli(cli);

        WalletPlugin p;
        auto r = parseObj(p.createAccount());
        QCOMPARE(r[QStringLiteral("id")].toString(), QString("public/new123"));
    }

    // ── sendTransfer validation ───────────────────────────────────────────────
    void testSendTransferMissingFrom()
    {
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.sendTransfer(QStringLiteral(""), QStringLiteral("public/b"), QStringLiteral("10"), kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testSendTransferMissingTo()
    {
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.sendTransfer(QStringLiteral("public/a"), QStringLiteral(""), QStringLiteral("10"), kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testSendTransferMissingAmount()
    {
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.sendTransfer(QStringLiteral("public/a"), QStringLiteral("public/b"), QStringLiteral(""), kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testSendTransferSuccess()
    {
        QString cli = makeFakeCli(R"({"ok":true,"txId":"tx123"})");
        useCli(cli);

        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.sendTransfer(
            QStringLiteral("public/a"),
            QStringLiteral("public/b"),
            QStringLiteral("10"), kPw));
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
        useCli(cli);

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
        useCli(cli);

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
        useCli(cli);

        WalletPlugin p;
        auto r = parseObj(p.syncPrivate());
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
    }

    // ── Privacy transfers - validation ───────────────────────────────────────────
    void testShieldMissingAmount()
    {
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.startShield(QStringLiteral("native"),
                                        QStringLiteral("Public/a"),
                                        QStringLiteral("Private/b"),
                                        QStringLiteral(""), QString(), kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testShieldRejectsPrivateSource()
    {
        // Shield source must be Public - a Private/ source is a prefix conflict.
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.startShield(QStringLiteral("native"),
                                        QStringLiteral("Private/a"),
                                        QStringLiteral("Private/b"),
                                        QStringLiteral("10"), QString(), kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testDeshieldRejectsPublicSource()
    {
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.startDeshield(QStringLiteral("native"),
                                          QStringLiteral("Public/a"),
                                          QStringLiteral("Public/b"),
                                          QStringLiteral("10"), QString(), kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testForeignTransferRequiresKeys()
    {
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.startPrivateTransferForeign(QStringLiteral("native"),
                                                        QStringLiteral("Private/a"),
                                                        QStringLiteral(""),   // npk missing
                                                        QStringLiteral("vpk"),
                                                        QStringLiteral("id"),
                                                        QStringLiteral("10"), kPw));
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
        useCli(cli);

        WalletPlugin p;
        arm(p);
        auto started = parseObj(p.startShield(QStringLiteral("native"),
                                              QStringLiteral("Public/a"),
                                              QStringLiteral("Private/b"),
                                              QStringLiteral("10"), QString(), kPw));
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
        useCli(cli);

        WalletPlugin p;
        arm(p);
        auto started = parseObj(p.startPrivateTransfer(QStringLiteral("token"),
                                                       QStringLiteral("Private/a"),
                                                       QStringLiteral("Private/b"),
                                                       QStringLiteral("5"), kPw));
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
        useCli(cli);

        WalletPlugin p;
        arm(p);
        auto started = parseObj(p.startDeshield(QStringLiteral("native"),
                                                QStringLiteral("Private/a"),
                                                QStringLiteral("Public/b"),
                                                QStringLiteral("10"), QString(), kPw));
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
        arm(p);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), true);
        p.clearSessionPassword();
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
    }

    void testPasswordPipedToStdin()
    {
        QString cli = makeStdinEchoCli();
        useCli(cli);

        armStore();                    // unlock() refuses when there is no store to open
        WalletPlugin p;
        // unlock() is what establishes a session now; the echo CLI accepts it and reports back
        // the password it was fed on stdin.
        QVERIFY(!parseObj(p.unlock(QStringLiteral("s3cret"))).contains(QStringLiteral("error")));
        // listAccounts runs the CLI, which echoes the piped password back.
        auto r = parseObj(p.listAccounts());
        QCOMPARE(r[QStringLiteral("pw")].toString(), QString("s3cret"));
    }

    void testUnlockWrongPassword()
    {
        QString cli = makeFakeCli(R"({"error":"Failed to decrypt wallet storage"})", 1);
        useCli(cli);

        armStore();
        WalletPlugin p;
        auto r = parseObj(p.unlock(QStringLiteral("wrong")));
        QVERIFY(r.contains(QStringLiteral("error")));
        // Wrong password must be cleared again.
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
    }

    void testUnlockSuccess()
    {
        QString cli = makeFakeCli(R"([{"id":"Public/abc","type":"public","balance":5}])");
        useCli(cli);

        armStore();
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
        useCli(cli);

        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.exportMnemonic(kPw));
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        QVERIFY(r[QStringLiteral("mnemonic")].toString().startsWith(QStringLiteral("legal winner")));
    }

    void testExportKeyMissingId()
    {
        WalletPlugin p;
        arm(p);
        QVERIFY(parseObj(p.exportKey(QStringLiteral(""), kPw)).contains(QStringLiteral("error")));
    }

    void testExportKeySuccess()
    {
        QString cli = makeFakeCli("10a26a9aec7d34b82364eeae45c5294dbb0a764b000b94eeb9b58511dc487c4d");
        useCli(cli);

        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.exportKey(QStringLiteral("Public/abc"), kPw));
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
        useCli(cli);

        WalletPlugin p;
        auto r = parseObj(p.importKey(QStringLiteral("deadbeef"), QStringLiteral("mine")));
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        QCOMPARE(r[QStringLiteral("id")].toString(), QString("Public/GkeQajoUJ6KUz"));
    }

    void testRestoreWalletValidation()
    {
        WalletPlugin p;
        QVERIFY(parseObj(p.restoreWallet(QStringLiteral(""), QStringLiteral("pw"), 5, QString()))
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
        useCli(cli);

        WalletPlugin p;
        arm(p);
        const QString sid = connectAndApprove(p, {QStringLiteral("shield")},
                                              {QStringLiteral("Public/a")});
        auto ra = parseObj(p.requestAction(sid, QStringLiteral(
            "{\"op\":\"shield\",\"from\":\"Public/a\",\"to\":\"Private/b\",\"amount\":\"7\"}")));
        const QString reqId = ra[QStringLiteral("requestId")].toString();

        auto ap = parseObj(p.approveAction(reqId, kPw));
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
        arm(p);
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
        auto ap = parseObj(p.approveAction(reqId, kPw));
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
        arm(p);
        auto cr = parseObj(p.connectRequest(QStringLiteral("{\"appName\":\"x\"}"),
                                            QStringLiteral("[\"send\"]")));
        const QString reqId = cr[QStringLiteral("requestId")].toString();
        auto r = parseObj(p.approveAction(reqId, kPw));
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

    // ── Sequencer status: the offline/failure surface the UI's modal + banner read ──
    void testSequencerStatusDevnetBinaryMissing()
    {
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/network"), QStringLiteral("devnet"));
        s.sync();
        // The sequencer binary is pointed at through the ENVIRONMENT now, exactly like the wallet
        // CLI. It used to be pointed at through medusa-wallet/seqPath in QSettings, which is a
        // user-writable INI a co-resident module writes directly - that was unauthenticated
        // arbitrary code execution, reached through the ungated setActiveZone.
        useSeq(m_tmp.path() + QStringLiteral("/no_such_sequencer"));

        WalletPlugin p;
        auto r = parseObj(p.getSequencerStatus());
        QCOMPARE(r[QStringLiteral("mode")].toString(), QString("local-standalone"));
        QCOMPARE(r[QStringLiteral("binaryAvailable")].toBool(), false);
        QCOMPARE(r[QStringLiteral("running")].toBool(), false);
        QCOMPARE(r[QStringLiteral("healthy")].toBool(), false);
        // No binary can never come up - reported immediately, no launch grace applies.
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("binary-missing"));
        QCOMPARE(r[QStringLiteral("compat")].toString(), QString("unknown"));
        QVERIFY(r.contains(QStringLiteral("endpoint")));
        QVERIFY(r.contains(QStringLiteral("lastLaunchError")));
    }

    void testSequencerStatusLaunchFailedReason()
    {
        // An existing but non-executable "binary": the spawn fails, and the status must
        // carry the reason + a human launch error (not just an eternal "unreachable").
        QString bad = m_tmp.path() + QStringLiteral("/seq_not_executable");
        { QFile f(bad); f.open(QIODevice::WriteOnly); f.write("not a program\n"); }
        QFile::setPermissions(bad, QFile::ReadOwner | QFile::WriteOwner);   // no exec bit

        useSeq(bad);

        WalletPlugin p;
        parseObj(p.setActiveZone(QStringLiteral("devnet")));   // applySequencer → spawn attempt
        auto r = parseObj(p.getSequencerStatus());
        QCOMPARE(r[QStringLiteral("mode")].toString(), QString("local-standalone"));
        QCOMPARE(r[QStringLiteral("binaryAvailable")].toBool(), true);
        QCOMPARE(r[QStringLiteral("running")].toBool(), false);
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("launch-failed"));
        QVERIFY(!r[QStringLiteral("lastLaunchError")].toString().isEmpty());
    }

    void testSequencerStatusExitedReason()
    {
        // A "sequencer" that starts fine and exits immediately (crash / bad config / port
        // clash): the status must report reason "exited" with the exit code and the log
        // path in the module's data dir - even inside the launch grace window.
        QString dying = m_tmp.path() + QStringLiteral("/seq_dies.sh");
        { QFile f(dying); f.open(QIODevice::WriteOnly | QIODevice::Text);
          f.write("#!/bin/sh\necho boom\nexit 3\n"); }
        QFile::setPermissions(dying, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        useSeq(dying);

        WalletPlugin p;
        parseObj(p.setActiveZone(QStringLiteral("devnet")));
        QTRY_COMPARE_WITH_TIMEOUT(
            parseObj(p.getSequencerStatus())[QStringLiteral("reason")].toString(),
            QString("exited"), 5000);
        auto r = parseObj(p.getSequencerStatus());
        QCOMPARE(r[QStringLiteral("exitCode")].toInt(), 3);
        QVERIFY(r[QStringLiteral("lastLaunchError")].toString().isEmpty());
        QVERIFY(r[QStringLiteral("logPath")].toString().endsWith(QStringLiteral("sequencer.log")));
    }

    void testSequencerStatusRemoteFields()
    {
        // Fresh settings → the default zone is the built-in remote clearnet zone. The
        // status must expose the endpoint/healthy/compat trio there too (the offline
        // modal names the endpoint), but none of the local-sequencer reason fields.
        WalletPlugin p;
        auto r = parseObj(p.getSequencerStatus());
        QCOMPARE(r[QStringLiteral("mode")].toString(), QString("remote"));
        QVERIFY(r.contains(QStringLiteral("endpoint")));
        QVERIFY(r.contains(QStringLiteral("healthy")));
        QCOMPARE(r[QStringLiteral("compat")].toString(), QString("unknown"));
        QVERIFY(!r.contains(QStringLiteral("reason")));
        QVERIFY(!r.contains(QStringLiteral("running")));
    }

    // ── Proof-of-user gate ───────────────────────────────────────────────────────
    // These drive the REAL Q_INVOKABLEs, not authorize(). A previous round of work on this
    // module shipped helper-only tests that stayed green while the call sites were bypassed, so
    // every entry below is the actual method a hostile caller would reach over callModule.


    // Locked is the state a co-resident module finds the wallet in most of the time, and the
    // gate must fail CLOSED there rather than treating "no password set" as "none needed".
    void testGatedVerbsRefuseWhileLocked()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        for (const auto& v : gatedVerbs()) {
            WalletPlugin p;   // fresh: never unlocked
            const auto r = parseObj(v.second(p, kPw));
            QVERIFY2(r.contains(QStringLiteral("error")), qPrintable(v.first));
            QVERIFY2(r[QStringLiteral("reason")].toString() == QStringLiteral("locked"),
                     qPrintable(v.first + QStringLiteral(" did not refuse while locked")));
        }
    }

    // THE regression test for the whole design: an unlocked wallet plus a caller that does not
    // know the password. A hostile module can hold a valid bearer token and can wait for the
    // user to unlock; the password is the one thing it cannot obtain. If a gate is removed from
    // a verb, that verb proceeds here and this fails on its row.
    void testGatedVerbsRefuseWrongPassword()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        for (const auto& v : gatedVerbs()) {
            WalletPlugin p;
            arm(p);                                   // wallet unlocked, session password = kPw
            const auto r = parseObj(v.second(p, QStringLiteral("not the password")));
            QVERIFY2(r.contains(QStringLiteral("error")), qPrintable(v.first));
            QVERIFY2(r[QStringLiteral("reason")].toString() == QStringLiteral("unauthorized"),
                     qPrintable(v.first + QStringLiteral(" accepted a wrong password")));
        }
    }

    // …and the gate must not simply refuse everything: with the real password each verb gets
    // past it and fails (or succeeds) on its own merits.
    void testGatedVerbsAcceptCorrectPassword()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        for (const auto& v : gatedVerbs()) {
            WalletPlugin p;
            arm(p);
            const auto r = parseObj(v.second(p, kPw));
            const QString reason = r[QStringLiteral("reason")].toString();
            QVERIFY2(reason != QStringLiteral("locked") && reason != QStringLiteral("unauthorized"),
                     qPrintable(v.first + QStringLiteral(" refused the correct password")));
            QTest::qWait(30);   // let a start* job's fake CLI exit before p goes out of scope
        }
    }

    // A near-miss must not leak how near it was. This cannot observe timing directly, but it
    // pins the behaviour the constant-time compare exists for: no prefix or length shortcut.
    void testGateRejectsPrefixAndLengthVariants()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        for (const QString& guess : { QString(), kPw.left(kPw.size() - 1), kPw + QStringLiteral("x"),
                                      kPw.toUpper() }) {
            WalletPlugin p;
            arm(p);
            const auto r = parseObj(p.exportMnemonic(guess));
            QCOMPARE(r[QStringLiteral("reason")].toString(), QString("unauthorized"));
        }
    }

    // ── The three conditionally gated verbs ──────────────────────────────────────
    // They must stay usable while locked, because that is exactly the state of the user this
    // module has to keep serving: the one who forgot the password and is recovering from a
    // phrase. What makes that safe is that neither one destroys anything any more.

    QString writeStorage(const QString& body)
    {
        const QString home = qEnvironmentVariable("LEE_WALLET_HOME_DIR");
        QDir().mkpath(home);
        const QString storage = home + QStringLiteral("/storage.json");
        QFile f(storage);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write(body.toUtf8());
        f.close();
        return storage;
    }

    void testResetWalletMovesStorageAsideWhileLocked()
    {
        const QString storage = writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"x\"}"));
        WalletPlugin p;                       // locked: the forgot-my-password user
        auto r = parseObj(p.resetWallet());
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        const QString backup = r[QStringLiteral("backup")].toString();
        QVERIFY(!backup.isEmpty());
        QVERIFY(!QFile::exists(storage));     // the wallet is out of the way…
        QVERIFY(QFile::exists(backup));       // …but NOT destroyed: a wipe is now undoable
        QVERIFY(slurp(backup).contains(QStringLiteral("argon2id")));
        QFile::remove(backup);
    }

    void testResetWalletNeedsPasswordWhenUnlocked()
    {
        const QString storage = writeStorage(QStringLiteral("{\"kdf\":\"argon2id\"}"));
        WalletPlugin p;
        arm(p);
        // The check is against the STORE now (the CLI is asked to open it), not against the
        // in-memory session, so the fake CLI has to be one that actually checks a password.
        useCli(makePasswordCheckingCli(kPw));
        auto r = parseObj(p.resetWallet(QStringLiteral("wrong")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("unauthorized"));
        QVERIFY(QFile::exists(storage));      // and nothing was touched
        // A live session with NOTHING presented is refused too: the user has the password, a
        // caller that does not is not the user.
        QCOMPARE(parseObj(p.resetWallet())[QStringLiteral("reason")].toString(),
                 QString("unauthorized"));
        QVERIFY(QFile::exists(storage));
        // …and the real password still resets.
        auto ok = parseObj(p.resetWallet(kPw));
        QCOMPARE(ok[QStringLiteral("ok")].toBool(), true);
        QVERIFY(!QFile::exists(storage));
        QFile::remove(ok[QStringLiteral("backup")].toString());
    }

    // Review 2, H1 step 2: round 2 gated this verb on `sessionIsProvable()`, which is
    // `!m_password.isEmpty() && !storageIsPlaintext()` - and clearSessionPassword() is ungated by
    // design, so ONE call flipped the gate off. The condition is a fact about the store now, and
    // the credential is checked against the store, so clearing the session changes nothing for a
    // caller that presents a password.
    void testClearingTheSessionDoesNotUngateResetForAPresentedPassword()
    {
        const QString storage = writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"v\"}"));
        WalletPlugin p;
        arm(p);
        useCli(makePasswordCheckingCli(kPw));
        p.clearSessionPassword();             // step 1 of the exploit: ungated, by design
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        // A wrong password is still refused, session or no session.
        QCOMPARE(parseObj(p.resetWallet(QStringLiteral("wrong")))[QStringLiteral("reason")].toString(),
                 QString("unauthorized"));
        QVERIFY(QFile::exists(storage));
        QFile::remove(storage);
    }

    void testRestoreWalletWhileLockedBacksUpFirst()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        const QString storage = writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"old\"}"));
        WalletPlugin p;                       // locked: the phrase is the credential here
        auto r = parseObj(p.restoreWallet(QStringLiteral("legal winner thank year"),
                                          QStringLiteral("newpw"), 2, QString()));
        QVERIFY(!r.contains(QStringLiteral("error")));
        const QString backup = r[QStringLiteral("backup")].toString();
        QVERIFY(!backup.isEmpty());
        QVERIFY(slurp(backup).contains(QStringLiteral("old")));   // the replaced store survived
        // …but a LOCKED restore does NOT establish a session: see
        // testLockedRestoreDoesNotMintASession. The user unlocks with the password they chose.
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        QFile::remove(backup);
        QFile::remove(storage);
    }

    // Review 2, F4. restoreWallet is ungated while locked on purpose - the recovery phrase is the
    // credential, and the user who forgot their password has no session by definition. What it
    // must not do is seal the new store with the CALLER's password and then keep that password as
    // the session, because the caller then knows a credential that satisfies every gated verb.
    void testLockedRestoreDoesNotMintASession()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"victim\"}"));
        WalletPlugin p;                       // locked
        auto r = parseObj(p.restoreWallet(QStringLiteral("attacker phrase words here"),
                                          QStringLiteral("P"), 2, QString()));
        QVERIFY(!r.contains(QStringLiteral("error")));       // the restore itself still works
        // No session was handed out…
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        // …so the password the caller chose proves nothing.
        for (const auto& v : gatedVerbs()) {
            const auto g = parseObj(v.second(p, QStringLiteral("P")));
            QVERIFY2(g[QStringLiteral("reason")].toString() == QStringLiteral("locked"),
                     qPrintable(v.first + QStringLiteral(" accepted a restore-minted password")));
        }
        QFile::remove(r[QStringLiteral("backup")].toString());
    }

    // The other half of the same rule: a caller that PROVED the live session keeps working, and
    // the session follows the store to its new password. Breaking this would log a real user out
    // in the middle of their own restore.
    void testProvenRestoreCarriesTheSessionToTheNewPassword()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;
        arm(p);
        // A CLI that WRITES the restored store: the session is carried over by opening the new
        // store with the new password, so there has to be a new store to open.
        useCli(makeStoreWritingCli(R"({"ok":true})"));
        auto r = parseObj(p.restoreWallet(QStringLiteral("legal winner thank year"),
                                          QStringLiteral("newpw"), 2, kPw));
        QVERIFY(!r.contains(QStringLiteral("error")));
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), true);
        // The session is the NEW password now, not the old one.
        QVERIFY(parseObj(p.exportMnemonic(QStringLiteral("newpw")))[QStringLiteral("reason")]
                    .toString() != QStringLiteral("unauthorized"));
        QCOMPARE(parseObj(p.exportMnemonic(kPw))[QStringLiteral("reason")].toString(),
                 QString("unauthorized"));
    }

    // A mistyped phrase must not cost the user their wallet: the store is moved aside before
    // restore-keys runs, so a failure has to put it back.
    void testFailedRestorePutsTheStoreBack()
    {
        useCli(makeFakeCli(R"({"error":"Invalid mnemonic phrase"})", 1));
        const QString storage = writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"mine\"}"));
        WalletPlugin p;
        auto r = parseObj(p.restoreWallet(QStringLiteral("not a real phrase"),
                                          QStringLiteral("newpw"), 2, QString()));
        QVERIFY(r.contains(QStringLiteral("error")));
        QVERIFY(QFile::exists(storage));
        QCOMPARE(slurp(storage), QString("{\"kdf\":\"argon2id\",\"ct\":\"mine\"}"));
        QFile::remove(storage);
    }

    void testRestoreWalletNeedsPasswordWhenUnlocked()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;
        arm(p);
        useCli(makePasswordCheckingCli(kPw));   // the presented credential is checked against the store
        auto r = parseObj(p.restoreWallet(QStringLiteral("legal winner thank year"),
                                          QStringLiteral("newpw"), 2, QStringLiteral("wrong")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("unauthorized"));
        // …and clearing the session first does not turn the checked path into the unchecked one.
        p.clearSessionPassword();
        auto r2 = parseObj(p.restoreWallet(QStringLiteral("legal winner thank year"),
                                           QStringLiteral("newpw"), 2, QStringLiteral("wrong")));
        QCOMPARE(r2[QStringLiteral("reason")].toString(), QString("unauthorized"));
    }

    // ══ INVARIANT A ═══════════════════════════════════════════════════════════════════════════
    // Rounds 1 and 2 each closed a session mint and each shipped another one. The tests below are
    // the reviewers' exact sequences, plus a source-level enumeration so that a NEW writer of
    // m_password fails the suite even if nobody thinks to write a behavioural test for it.

    // Review 2's H1, verbatim: three ungated calls on a HEALTHY ENCRYPTED wallet.
    //     clearSessionPassword()      -> ungated by design; sessionIsProvable() now false
    //     resetWallet("")             -> round 2's gate was skipped, store renamed aside
    //     createEncryptedWallet("A")  -> succeeded because the store was gone, and SET m_password
    // …after which "A" satisfied all 12 gated verbs and approveZone repointed the wallet.
    // The first two steps still work (see resetWallet's comment for why gating them protects
    // nothing an attacker cannot do with rename(2)). The third grants nothing, which is what
    // makes the composition worthless.
    void testDestroyThenCreateMintsNoSession()
    {
        writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"victim\"}"));
        WalletPlugin p;
        arm(p);                                     // the user is unlocked and working
        useCli(makeStoreWritingCli("Generated new account with account_id Public/attacker at path 0"));

        p.clearSessionPassword();                   // step 1
        auto rst = parseObj(p.resetWallet(QString()));   // step 2
        QCOMPARE(rst[QStringLiteral("ok")].toBool(), true);
        const QString victimBackup = rst[QStringLiteral("backup")].toString();
        QVERIFY2(QFile::exists(victimBackup), "the victim's store was destroyed, not moved aside");

        auto cre = parseObj(p.createEncryptedWallet(QStringLiteral("A")));   // step 3
        QCOMPARE(cre[QStringLiteral("ok")].toBool(), true);   // it is a legitimate first-run verb
        // …and THIS is the step that used to complete the exploit.
        QCOMPARE(cre[QStringLiteral("locked")].toBool(), true);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        for (const auto& v : gatedVerbs()) {
            const auto g = parseObj(v.second(p, QStringLiteral("A")));
            QVERIFY2(g[QStringLiteral("reason")].toString() == QStringLiteral("locked"),
                     qPrintable(v.first + QStringLiteral(" accepted a wipe-then-create password")));
        }
        // The user can find the store that was moved out from under them.
        auto ws = parseObj(p.getWalletState());
        QJsonArray displaced = ws[QStringLiteral("displacedStores")].toArray();
        bool listed = false;
        for (const auto& d : displaced) listed = listed || (d.toString() == victimBackup);
        QVERIFY2(listed, "the displaced store is not reported anywhere");
        QFile::remove(victimBackup);
    }

    // On a FRESH install `account list` makes the CLI create a wallet sealed with whatever
    // password it is handed. unlock("anything") therefore used to create a wallet and return a
    // live session for it - a credential-free mint through unlock() itself, on the one code path
    // whose entire job is to verify. Neither review found this one; the store check that closes
    // it is what this pins.
    void testUnlockOnAnEmptyWalletHomeMintsNothingAndCreatesNothing()
    {
        // A CLI that does what the real one does on empty storage: creates the wallet.
        useCli(makeStoreWritingCli("[]"));
        WalletPlugin p;
        auto r = parseObj(p.unlock(QStringLiteral("attacker-chosen")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("no-wallet"));
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        QVERIFY2(!parseObj(p.getWalletState())[QStringLiteral("exists")].toBool(),
                 "unlock() on an empty home created a wallet");
        for (const auto& v : gatedVerbs())
            QVERIFY2(parseObj(v.second(p, QStringLiteral("attacker-chosen")))
                         [QStringLiteral("reason")].toString() == QStringLiteral("locked"),
                     qPrintable(v.first + QStringLiteral(" accepted an unlock-minted password")));
    }

    // ── The enumeration, as a test ───────────────────────────────────────────────────────────
    // Every round so far was defeated by the SAME capability reappearing at a new call site, so
    // the rule is checked over the whole file rather than at the sites anyone remembered to
    // think about. If a future change writes m_password anywhere else, this fails by name and
    // whoever wrote it has to justify a second writer instead of quietly having one.
#ifdef QT_TESTCASE_SOURCEDIR
    static QString pluginSource()
    {
        return slurp(QStringLiteral(QT_TESTCASE_SOURCEDIR)
                     + QStringLiteral("/src/plugin/WalletPlugin.cpp"));
    }

    void testSessionPasswordHasExactlyOneWriterAndOneClear()
    {
        const QString src = pluginSource();
        QVERIFY2(!src.isEmpty(), "could not read WalletPlugin.cpp");

        QStringList writers, clears;
        const QStringList lines = src.split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            const QString l = lines.at(i);
            if (l.trimmed().startsWith(QLatin1String("//")))
                continue;                                  // a comment quoting the rule is fine
            static const QRegularExpression assign(
                QStringLiteral("m_password\\s*(=[^=]|\\+=|\\.(setRawData|swap|resize|append|prepend|insert|replace|fill)\\s*\\()"));
            if (assign.match(l).hasMatch())
                writers << QStringLiteral("%1: %2").arg(i + 1).arg(l.trimmed());
            if (l.contains(QStringLiteral("m_password.clear()")))
                clears << QStringLiteral("%1: %2").arg(i + 1).arg(l.trimmed());
        }
        QVERIFY2(writers.size() == 1,
                 qPrintable(QStringLiteral("m_password must be assigned in exactly one place "
                                           "(establishSession). Found %1:\n%2")
                                .arg(writers.size()).arg(writers.join(QLatin1Char('\n')))));
        QVERIFY2(writers.first().contains(QStringLiteral("m_password = candidate")),
                 qPrintable(QStringLiteral("the one writer is not establishSession's verified "
                                           "assignment: ") + writers.first()));
        QVERIFY2(clears.size() == 1,
                 qPrintable(QStringLiteral("m_password must be cleared in exactly one place "
                                           "(clearSessionPassword). Found %1:\n%2")
                                .arg(clears.size()).arg(clears.join(QLatin1Char('\n')))));
    }

    // ══ INVARIANT B, as an enumeration ════════════════════════════════════════════════════════
    // Every process launch in the module, with the expression that supplies its PROGRAM. A new
    // call site whose program comes from somewhere new fails here and has to be justified.
    //
    // This test USED TO ALLOWLIST BARE NAMES - `QStringLiteral("curl")`, `"bash"`, `"which"`,
    // `"python3"` - and call them "a known source". They are not: a bare name is resolved by
    // QProcess against the PATH this process inherited, and on an ordinary desktop that PATH
    // begins with ~/.local/bin, which is writable by the co-resident module the whole design
    // defends against. Review 3 planted ~/.local/bin/curl and the 10-second status poll executed
    // it, unauthenticated, on a packaged install. The test was certifying the vulnerability, which
    // is the third time a passing test has pinned an attacker-reachable outcome as desirable, so
    // the rule it checks is now the opposite one: NO PROGRAM MAY BE A BARE NAME, ANYWHERE.
    // The source with comment lines removed: the file documents the primitives it closed (grep
    // recipes, the old `bash -c 'exec 3<>/dev/tcp/…'` line), and a scan that counted those would
    // be measuring prose. Every assertion below is about CODE.
    static QString pluginCode()
    {
        QStringList code;
        for (const QString& l : pluginSource().split(QLatin1Char('\n')))
            if (!l.trimmed().startsWith(QLatin1String("//"))) code << l;
        return code.join(QLatin1Char('\n'));
    }

    void testNoProcessIsEverLaunchedByABareName()
    {
        const QString src = pluginCode();
        QVERIFY2(!src.isEmpty(), "could not read WalletPlugin.cpp");

        // 1. QProcess::start() itself appears exactly once, inside startChild(). Everything else
        //    that calls .start() is a QTimer (idleLockMs()) or a QElapsedTimer (no argument).
        static const QRegularExpression anyStart(
            QStringLiteral("(?:->|\\.)start\\(\\s*([A-Za-z_][A-Za-z0-9_]*)?"));
        QStringList startArgs, badStarts;
        QRegularExpressionMatchIterator sit = anyStart.globalMatch(src);
        while (sit.hasNext()) {
            const QString a = sit.next().captured(1).trimmed();
            startArgs << a;
            if (a != QStringLiteral("program")        // QProcess, inside startChild()
                && a != QStringLiteral("idleLockMs")  // the idle-lock QTimer
                && !a.isEmpty())                      // QElapsedTimer (no argument)
                badStarts << a;
        }
        QVERIFY2(badStarts.isEmpty(),
                 qPrintable(QStringLiteral("a .start() outside startChild(): ")
                            + badStarts.join(QStringLiteral(", "))));
        QCOMPARE(startArgs.count(QStringLiteral("program")), 1);   // the one QProcess::start

        // 2. startChild() refuses anything that is not an absolute path. This is the enforcement;
        //    the allowlist below is only there to make a new call site visible.
        QVERIFY2(src.contains(QStringLiteral("!program.startsWith(QLatin1Char('/'))")),
                 "startChild no longer refuses a program that is not an absolute path");

        // 3. Every startChild() call site's PROGRAM expression, and where it came from.
        //    bin/curl/torBin/py/fwd are locals; the assertions under (4) pin what each is
        //    assigned from, so this list cannot be satisfied by a same-named local of any origin.
        const QStringList allowed{
            QStringLiteral("cliPath()"),  // = resolveBin("wallet", MEDUSA_WALLET_CLI)
            QStringLiteral("bin"),        // = cliPath() / seqPath()
            QStringLiteral("curl"),       // = curlPath()   -> resolveSystemBin, never $PATH
            QStringLiteral("torBin"),     // = resolveBin("medusa-tor") / resolveSystemBin("tor")
            QStringLiteral("py"),         // = resolveSystemBin("python3")
            QStringLiteral("fwd"),        // = resolveBin("diaphani-forward", MEDUSA_FORWARD_BIN)
        };
        static const QRegularExpression call(
            QStringLiteral("startChild\\(\\s*[^,]+,\\s*"
                           "([A-Za-z_][A-Za-z0-9_]*(?:\\(\\))?"
                           "|QStringLiteral\\(\"[^\"]*\"\\))\\s*[,)]"));
        QRegularExpressionMatchIterator it = call.globalMatch(src);
        QStringList seen, bad;
        while (it.hasNext()) {
            const QString prog = it.next().captured(1);
            if (prog == QStringLiteral("program"))
                continue;                       // startChild's own declaration/definition
            seen << prog;
            if (!allowed.contains(prog)) bad << prog;
        }
        QVERIFY2(bad.isEmpty(),
                 qPrintable(QStringLiteral("startChild with an unrecognised program source: ")
                            + bad.join(QStringLiteral(", "))));
        // Sanity: the scan really found the call sites (a broken regex must not pass silently).
        QVERIFY2(seen.size() >= 10, qPrintable(QStringLiteral("only found %1 launch sites")
                                                   .arg(seen.size())));
        // …and not one of them is a literal name.
        QVERIFY2(!src.contains(QRegularExpression(
                     QStringLiteral("startChild\\([^,]+,\\s*QStringLiteral"))),
                 "a startChild() call site launches a literal program name");

        // 4. The locals really are resolver output, and the two shells are gone from the module
        //    entirely - a port probe and a binary lookup need no child process at all, so those
        //    launches were deleted rather than guarded.
        QVERIFY(src.contains(QStringLiteral("const QString curl = curlPath();")));
        QVERIFY(src.contains(QStringLiteral("resolveSystemBin(QStringLiteral(\"curl\"), \"MEDUSA_CURL_BIN\")")));
        QVERIFY(src.contains(QStringLiteral("const QString py  = resolveSystemBin(QStringLiteral(\"python3\")")));
        QVERIFY(src.contains(QStringLiteral("const QString fwd = resolveBin(QStringLiteral(\"diaphani-forward\")")));
        QCOMPARE(src.count(QStringLiteral("QStringLiteral(\"bash\")")), 0);
        QCOMPARE(src.count(QStringLiteral("QStringLiteral(\"which\")")), 0);
        QCOMPARE(src.count(QStringLiteral("/dev/tcp/")), 0);
        // python3 survives only as a NAME HANDED TO THE RESOLVER, never as a program.
        QCOMPARE(src.count(QStringLiteral("QStringLiteral(\"python3\")")),
                 src.count(QStringLiteral("resolveSystemBin(QStringLiteral(\"python3\")")));
        // resolveBin's last resort used to be `return name;` - a bare name for a $PATH lookup.
        QCOMPARE(src.count(QRegularExpression(QStringLiteral("^\\s*return name;\\s*$"),
                                              QRegularExpression::MultilineOption)), 0);

        // 5. And no path a process is launched from may come out of QSettings. The two keys that
        //    ever did are read in exactly one reporting function each, and nowhere else.
        QCOMPARE(src.count(QStringLiteral("kSeqPathKey")), 2);   // the declaration + getSequencerConfig
        QCOMPARE(src.count(QStringLiteral("kCliPathKey")), 2);   // the declaration + getConfig
    }

    // The other half of invariant C, as a source rule: authorize() may look at the SECRET and at
    // nothing else. Every previous round died because a check was keyed on state the attacker
    // could write (QSettings for cliPath/seqPath, m_password.isEmpty() for round 2's recovery
    // gates, storage.json's first 256 bytes for round 3's gate), so the body is pinned here as
    // well as behaviourally: no file, no store predicate, no setting inside authorize().
    void testAuthorizeConsultsNothingButTheSecret()
    {
        // Comment lines are already stripped: the body documents the hole it closed, and quoting
        // a rule is fine.
        const QString src = pluginCode();
        const int start = src.indexOf(QStringLiteral("bool WalletPlugin::authorize("));
        QVERIFY2(start > 0, "authorize() not found");
        const int end = src.indexOf(QStringLiteral("\n}"), start);
        QVERIFY(end > start);
        const QString body = src.mid(start, end - start);

        for (const QString& banned : { QStringLiteral("storageIsPlaintext"),
                                       QStringLiteral("storageExists"),
                                       QStringLiteral("storeCanProvePassword"),
                                       QStringLiteral("QFile"), QStringLiteral("QSettings"),
                                       QStringLiteral("storagePath"), QStringLiteral("qEnvironmentVariable") })
            QVERIFY2(!body.contains(banned),
                     qPrintable(QStringLiteral("authorize() consults attacker-writable state: ")
                                + banned));
        QVERIFY(body.contains(QStringLiteral("m_password.isEmpty()")));
        QVERIFY(body.contains(QStringLiteral("constantTimeEquals(m_password, password)")));
    }
#endif

    // Review 2's F1/H3, verbatim, and the single most severe hole either review found: a
    // co-resident module writes medusa-wallet/seqPath into the shared user INI - no IPC, no
    // password, nothing to gate - and calls the UNGATED setActiveZone("devnet"). The wallet then
    // spawned the attacker's binary as the user, persistently, across reboots and across
    // uninstalling the module that planted it.
    void testPoisonedSeqPathSettingIsNeverExecuted()
    {
        const QString evil = m_tmp.path() + QStringLiteral("/evil_sequencer.sh");
        QFile::remove(evil + QStringLiteral(".ran"));
        {
            QFile f(evil);
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            f.write(QString("#!/bin/sh\necho ran > '%1.ran'\nsleep 5\n").arg(evil).toUtf8());
            f.close();
            QFile::setPermissions(evil, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
        QSettings s;                                     // written directly, as the attacker does
        s.setValue(QStringLiteral("medusa-wallet/seqPath"), evil);
        s.setValue(QStringLiteral("medusa-wallet/seqMode"), QStringLiteral("local"));
        s.setValue(QStringLiteral("medusa-wallet/seqUrl"), evil);
        s.sync();

        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;
        auto r = parseObj(p.setActiveZone(QStringLiteral("devnet")));   // ungated, by design
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);               // and it still works
        QTest::qWait(600);                                             // let any spawn happen
        QVERIFY2(!QFile::exists(evil + QStringLiteral(".ran")),
                 "the poisoned sequencer binary was EXECUTED");

        auto cfg = parseObj(p.getSequencerConfig());
        QVERIFY2(cfg[QStringLiteral("seqPathEff")].toString() != evil,
                 "the poisoned setting is still what would be executed");
        QCOMPARE(cfg[QStringLiteral("seqPath")].toString(), evil);          // still visible…
        QCOMPARE(cfg[QStringLiteral("seqPathIgnored")].toBool(), true);     // …and always disowned
        QCOMPARE(cfg[QStringLiteral("seqPathConfigurable")].toBool(), false);
        // setNetwork is the same verb by another name; it must not be a second door.
        QVERIFY(!parseObj(p.setNetwork(QStringLiteral("devnet"))).contains(QStringLiteral("error")));
        QTest::qWait(300);
        QVERIFY(!QFile::exists(evil + QStringLiteral(".ran")));
    }

    // Review 2A's F4: ~/.local/bin/<name> is writable at this uid, so on an install where a
    // bundled binary is missing an attacker can ADD one - no overwrite, no integrity signal.
    // A packaged install has a bundle directory, and the rule is that the bundle is then the ONLY
    // place looked at, so the plant is never selected even for a binary the bundle lacks. (This
    // test binary has a bundle dir: initTestCase creates it.)
    void testBundledInstallNeverResolvesOutsideItsOwnBinDir()
    {
        const QString bundleDir = QCoreApplication::applicationDirPath() + QStringLiteral("/bin");
        QVERIFY(QDir(bundleDir).exists());
        // sequencer_service is deliberately NOT in the bundle here - the case where the fallback
        // used to select a planted ~/.local/bin copy.
        QVERIFY(!QFile::exists(bundleDir + QStringLiteral("/sequencer_service")));

        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/network"), QStringLiteral("devnet"));
        s.sync();
        WalletPlugin p;
        const QString eff = parseObj(p.getSequencerConfig())[QStringLiteral("seqPathEff")].toString();
        QCOMPARE(eff, bundleDir + QStringLiteral("/sequencer_service"));
        QVERIFY2(!eff.contains(QStringLiteral("/.local/bin/")),
                 "a bundled install still falls back to ~/.local/bin");
        // The wallet CLI resolves the same way with no env override.
        qunsetenv("MEDUSA_WALLET_CLI");
        WalletPlugin p2;
        const QString cli = parseObj(p2.getConfig())[QStringLiteral("cliPathEff")].toString();
        QCOMPARE(cli, bundleDir + QStringLiteral("/wallet"));
    }

    // ══ REVIEW 3's F1, AS IT WAS RUN ══════════════════════════════════════════════════════════
    // A co-resident module drops executables into a directory that comes EARLIER in $PATH than
    // the real ones - on the reviewer's box ~/.local/bin, which ~/.profile prepends, and which
    // resolveBin's own comment calls attacker-writable - and waits. `curl`, `bash`, `python3` and
    // `which` were all launched as BARE NAMES, and getSequencerStatus (ungated, no wallet, no
    // password) polls curl every 10 seconds from app start. Unauthenticated arbitrary code
    // execution as the user, on a PACKAGED install, with nobody at the keyboard.
    //
    // Nothing in this module resolves a program through $PATH any more, so nothing here runs.
    QString writeExec(const QString& path, const QString& body)
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write(body.toUtf8());
        f.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

    void testPlantedHelpersEarlierInPathAreNeverExecuted()
    {
        const QString evilDir = m_tmp.path() + QStringLiteral("/attacker-bin");
        QDir().mkpath(evilDir);
        const QStringList planted{ QStringLiteral("curl"), QStringLiteral("bash"),
                                   QStringLiteral("python3"), QStringLiteral("which"),
                                   QStringLiteral("wallet"), QStringLiteral("sequencer_service"),
                                   QStringLiteral("sequencer_service_l1"),
                                   QStringLiteral("medusa-tor"), QStringLiteral("diaphani-forward"),
                                   QStringLiteral("medusa-tor-monitor") };
        for (const QString& n : planted) {
            QFile::remove(evilDir + QLatin1Char('/') + n + QStringLiteral(".ran"));
            writeExec(evilDir + QLatin1Char('/') + n,
                      QStringLiteral("#!/bin/sh\necho ran > '%1/%2.ran'\nexit 0\n").arg(evilDir, n));
        }
        const QByteArray prevPath = qgetenv("PATH");
        qputenv("PATH", (evilDir + QLatin1Char(':') + QString::fromLocal8Bit(prevPath)).toUtf8());
        // …and the same plant offered through the launcher-owned overrides as a BARE NAME, which
        // is the one remaining way a name could enter: it must be resolved, not handed to QProcess.
        qputenv("MEDUSA_CURL_BIN", "curl");
        qputenv("MEDUSA_WALLET_CLI", "wallet");

        QString listResult;
        {
            WalletPlugin p;
            p.getStatus();                                    // used to shell out to `which`
            p.setActiveZone(QStringLiteral("devnet"));        // sync health probe + sequencer spawn
            for (int i = 0; i < 3; ++i) {                     // the 10s status poll, three ticks
                p.getSequencerStatus();
                QTest::qWait(120);
            }
            listResult = p.listAccounts();                    // the CLI itself, by bare name
            QTest::qWait(200);
        }
        qputenv("PATH", prevPath);
        qunsetenv("MEDUSA_CURL_BIN");
        qunsetenv("MEDUSA_WALLET_CLI");

        QStringList ran;
        for (const QString& n : planted)
            if (QFile::exists(evilDir + QLatin1Char('/') + n + QStringLiteral(".ran")))
                ran << n;
        QVERIFY2(ran.isEmpty(),
                 qPrintable(QStringLiteral("the wallet EXECUTED planted $PATH binaries: ")
                            + ran.join(QStringLiteral(", "))));
        // A bare name that cannot be resolved is a refusal with a message, never a $PATH lookup.
        QVERIFY2(parseObj(listResult).contains(QStringLiteral("error")),
                 "a bare-name MEDUSA_WALLET_CLI was launched instead of being refused");
    }

    // …and the sanitising follows the child, because a child resolves ITS own helpers by name:
    // the shipped wallet CLI is a `#!/usr/bin/env python3` script, so a planted python3 would run
    // inside the wallet with the session password on its stdin.
    void testChildrenGetAPathWithNoUidWritableEntries()
    {
        const QString evilDir = m_tmp.path() + QStringLiteral("/attacker-bin2");
        QDir().mkpath(evilDir);
        const QString cli = m_tmp.path() + QStringLiteral("/pathrec_wallet.sh");
        QFile::remove(cli + QStringLiteral(".path"));
        writeExec(cli, QStringLiteral("#!/bin/sh\necho \"$PATH\" > '%1.path'\n"
                                      "echo '[]'\n").arg(cli));
        const QByteArray prevPath = qgetenv("PATH");
        qputenv("PATH", (evilDir + QLatin1Char(':') + QString::fromLocal8Bit(prevPath)).toUtf8());
        useCli(cli);

        { WalletPlugin p; p.listAccounts(); }
        const QString childPath = slurp(cli + QStringLiteral(".path")).trimmed();
        qputenv("PATH", prevPath);

        // The recording has to be real: an empty file (or a script that never ran) would make
        // every assertion below vacuously true, which is how this test was wrong the first time.
        QVERIFY2(!childPath.isEmpty(), "the child recorded no PATH at all");
        QVERIFY2(childPath.contains(QLatin1Char('/')), "the child's PATH was not recorded");
        const QStringList entries = childPath.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        QVERIFY(!entries.isEmpty());
        QVERIFY2(!entries.contains(evilDir), "the child inherited the attacker's PATH entry");
        for (const QString& e : entries) {
            QVERIFY2(!e.startsWith(QDir::homePath() + QLatin1Char('/')),
                     qPrintable(QStringLiteral("the child's PATH contains a home directory: ") + e));
            QVERIFY2(!QFileInfo(e).isWritable(),
                     qPrintable(QStringLiteral("the child's PATH contains a writable dir: ") + e));
        }
    }

    // ══ REVIEW 4's CRITICAL: the OTHER search list a child resolves code through ══════════════
    // $PATH decides which FILE runs. The interpreter's own import search decides what that file
    // then LOADS, and the shipped CLI is a `#!/usr/bin/env python3` script - so a complete
    // CPython startup happens inside the one process this module hands the session password to
    // on stdin. A co-resident module writes ONE ordinary file at this uid,
    // ~/.local/lib/pythonX.Y/site-packages/*.pth (no exec bit, no IPC, no user, no password),
    // site.py exec()s it, and it reads the password off fd 0 before the wrapper ever does.
    // Round 4 closed $PATH and its comment certified the whole class; this is the same assertion
    // for the mechanism that was left open.
    //
    // The shape matters: the POSITIVE CONTROL runs first, driving the very same shebang script
    // with the environment as it comes. If the plant does not fire there, the plant is broken
    // and every assertion below would be vacuously true - which is exactly how the $PATH test
    // was wrong the first time.
    void testAPlantedUserSitePthNeverExecutesInAChild()
    {
        const QString py = trustedPython3();
        if (py.isEmpty())
            QSKIP("no python3 in a trusted system directory: the shipped CLI's interpreter is "
                  "not installed here, so there is no user-site search to close");

        // The exact directory this interpreter would add: <HOME>/.local/lib/pythonX.Y/site-packages.
        QProcess ver;
        ver.start(py, { QStringLiteral("-c"),
                        QStringLiteral("import sys;print('%d.%d'%sys.version_info[:2])") });
        QVERIFY2(ver.waitForFinished(8000), "the interpreter never reported its version");
        const QString xy = QString::fromUtf8(ver.readAllStandardOutput()).trimmed();
        QVERIFY2(!xy.isEmpty(), "could not determine the interpreter's X.Y version");

        // A sandbox HOME, so the plant goes where CPython really looks WITHOUT writing anything
        // into the developer's own ~/.local. HOME is inherited by the child, so the child's
        // user-site directory is this one.
        const QString fakeHome = m_tmp.path() + QStringLiteral("/pyhome");
        const QString site = fakeHome + QStringLiteral("/.local/lib/python") + xy
                           + QStringLiteral("/site-packages");
        QVERIFY(QDir().mkpath(site));
        const QString marker = m_tmp.path() + QStringLiteral("/user-site.ran");
        QFile::remove(marker);

        // site.py exec()s any .pth line beginning with `import `. This is the round-4 PoC's file.
        QFile pth(site + QStringLiteral("/zz-attacker.pth"));
        QVERIFY(pth.open(QIODevice::WriteOnly | QIODevice::Text));
        pth.write(QStringLiteral("import os; open(r'%1','a').write('pth\\n')\n")
                      .arg(marker).toUtf8());
        pth.close();
        // usercustomize is the same directory's second mechanism. It is planted rather than
        // assumed to follow, because "one mechanism verified, the class declared closed" is the
        // mistake being corrected here.
        QFile uc(site + QStringLiteral("/usercustomize.py"));
        QVERIFY(uc.open(QIODevice::WriteOnly | QIODevice::Text));
        uc.write(QStringLiteral("import os; open(r'%1','a').write('usercustomize\\n')\n")
                     .arg(marker).toUtf8());
        uc.close();

        // A REAL `#!/usr/bin/env python3` CLI, like the shipped wallet-wrapper: the module never
        // names the interpreter for it, so this also proves the hardening survives both exec
        // hops (kernel -> /usr/bin/env -> python3) instead of assuming envp is carried across.
        const QString cli = m_tmp.path() + QStringLiteral("/pth_wallet.py");
        const QString sysPathOut = cli + QStringLiteral(".syspath");
        QFile::remove(sysPathOut);
        writeExec(cli, QStringLiteral("#!/usr/bin/env python3\n"
                                      "import sys\n"
                                      "open(r'%1','w').write('\\n'.join(sys.path))\n"
                                      "sys.stdin.read()\n"
                                      "print('[]')\n").arg(sysPathOut));

        const QByteArray prevPath = qgetenv("PATH");
        const QByteArray prevHome = qgetenv("HOME");
        // Guarantee the shebang can resolve: the interpreter's own (root-owned) directory
        // survives sanitizedPath(), so this does not weaken what is being tested.
        qputenv("PATH", (QFileInfo(py).absolutePath() + QLatin1Char(':')
                         + QString::fromLocal8Bit(prevPath)).toUtf8());
        qputenv("HOME", fakeHome.toUtf8());

        // ── positive control: the same script, the environment as inherited ──
        {
            QProcess ctrl;
            ctrl.start(cli, {});
            if (ctrl.waitForStarted(5000)) {
                ctrl.write("the-owners-password\n");
                ctrl.closeWriteChannel();
                ctrl.waitForFinished(15000);
            }
        }
        const QString controlRan = slurp(marker);

        // ── the module's own launch path ──
        QFile::remove(marker);
        QFile::remove(sysPathOut);
        useCli(cli);
        QString listResult;
        { WalletPlugin p; listResult = p.listAccounts(); }
        const QString afterRan  = slurp(marker);
        const QString childPath = slurp(sysPathOut);

        qputenv("PATH", prevPath);
        if (prevHome.isEmpty()) qunsetenv("HOME"); else qputenv("HOME", prevHome);

        QVERIFY2(controlRan.contains(QStringLiteral("pth")),
                 "the planted user-site .pth did NOT execute even without the hardening - the "
                 "plant is broken, so this test would prove nothing");
        QVERIFY2(!childPath.isEmpty(),
                 "the wallet CLI child never ran, so nothing below was actually tested");
        QVERIFY2(afterRan.isEmpty(),
                 qPrintable(QStringLiteral("a planted user-site file EXECUTED inside the wallet "
                                           "CLI (it also gets the password on stdin): ")
                            + afterRan.trimmed()));
        for (const QString& e : childPath.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            QVERIFY2(!e.startsWith(fakeHome),
                     qPrintable(QStringLiteral("the child's sys.path contains an entry under the "
                                               "attacker-writable home: ") + e));
        // …and the CLI still works, i.e. the isolation did not cost the product anything.
        QVERIFY2(!parseObj(listResult).contains(QStringLiteral("error")), qPrintable(listResult));
    }

    // …and the same assertion for the whole enumeration at once, rather than one mechanism at a
    // time: whatever a child's runtime consults, it must not arrive naming a place this uid can
    // write. Pins CPython's PYTHON* knobs, the dynamic loader's three, and Qt's four. A `#!/bin/sh`
    // stand-in dumps its own environment, so this needs no interpreter to be installed.
    void testChildEnvironmentNamesNoUidWritableCodeSource()
    {
        const QString envBin = trustedEnvBin();
        if (envBin.isEmpty())
            QSKIP("no `env` in a trusted system directory to dump the child environment with");
        const QString keptDir = QFileInfo(envBin).absolutePath();   // root-owned, must SURVIVE

        const QString evil = m_tmp.path() + QStringLiteral("/attacker-code");
        QDir().mkpath(evil);
        const QString evilSo = evil + QStringLiteral("/evil.so");
        { QFile f(evilSo); f.open(QIODevice::WriteOnly); f.write("not a real object"); }

        struct Var { const char* name; QString value; };
        const QVector<Var> planted{
            { "PYTHONPATH",      evil },                       // shadows the stdlib, runs sitecustomize
            { "PYTHONHOME",      evil },                       // relocates the whole stdlib
            { "PYTHONSTARTUP",   evil + QStringLiteral("/s.py") },
            { "PYTHONUSERBASE",  evil },
            { "LD_PRELOAD",      evilSo },                     // mapped in before main()
            { "LD_AUDIT",        evilSo },
            { "LD_LIBRARY_PATH", evil + QLatin1Char(':') + keptDir },
            { "GCONV_PATH",      evil },                       // glibc dlopen()s gconv modules
            { "QT_PLUGIN_PATH",  evil },
        };
        QVector<QPair<QByteArray, QByteArray>> prev;
        for (const Var& v : planted) {
            prev.append({ QByteArray(v.name), qgetenv(v.name) });
            qputenv(v.name, v.value.toUtf8());
        }

        const QString cli = m_tmp.path() + QStringLiteral("/envrec_wallet.sh");
        const QString envOut = cli + QStringLiteral(".env");
        QFile::remove(envOut);
        writeExec(cli, QStringLiteral("#!/bin/sh\n'%1' > '%2'\necho '[]'\n").arg(envBin, envOut));
        useCli(cli);
        { WalletPlugin p; p.listAccounts(); }
        const QString childEnvDump = slurp(envOut);

        for (const auto& p : prev) {
            if (p.second.isEmpty()) qunsetenv(p.first.constData());
            else                    qputenv(p.first.constData(), p.second);
        }

        QVERIFY2(!childEnvDump.isEmpty(), "the child recorded no environment at all");
        QHash<QString, QString> got;
        for (const QString& line : childEnvDump.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0) got.insert(line.left(eq), line.mid(eq + 1));
        }
        QVERIFY2(got.contains(QStringLiteral("PATH")), "the environment dump is not an environment");

        // CPython: every PYTHON* variable is dropped, and only the isolation flags come back.
        // Asserting on the PREFIX, not on a list of names, is the point - a knob added by a
        // future CPython release is covered without anyone remembering to add it here.
        static const QStringList kAllowedPython{
            QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("PYTHONSAFEPATH"),
            QStringLiteral("PYTHONBREAKPOINT"), QStringLiteral("PYTHONDONTWRITEBYTECODE")
        };
        for (auto it = got.cbegin(); it != got.cend(); ++it)
            if (it.key().startsWith(QStringLiteral("PYTHON")))
                QVERIFY2(kAllowedPython.contains(it.key()),
                         qPrintable(QStringLiteral("the child inherited a PYTHON* knob: ")
                                    + it.key() + QStringLiteral("=") + it.value()));
        QCOMPARE(got.value(QStringLiteral("PYTHONNOUSERSITE")), QStringLiteral("1"));
        QCOMPARE(got.value(QStringLiteral("PYTHONSAFEPATH")),   QStringLiteral("1"));

        // The dynamic loader: a planted object is dropped, a root-owned directory survives.
        QVERIFY2(!got.contains(QStringLiteral("LD_PRELOAD")),
                 qPrintable(QStringLiteral("the child inherited LD_PRELOAD=")
                            + got.value(QStringLiteral("LD_PRELOAD"))));
        QVERIFY2(!got.contains(QStringLiteral("LD_AUDIT")),
                 qPrintable(QStringLiteral("the child inherited LD_AUDIT=")
                            + got.value(QStringLiteral("LD_AUDIT"))));
        QCOMPARE(got.value(QStringLiteral("LD_LIBRARY_PATH")), keptDir);
        QVERIFY2(!got.contains(QStringLiteral("GCONV_PATH")),
                 qPrintable(QStringLiteral("the child inherited GCONV_PATH=")
                            + got.value(QStringLiteral("GCONV_PATH"))));

        // Qt: same filter, and the same "no writable entry" rule.
        QVERIFY2(!got.value(QStringLiteral("QT_PLUGIN_PATH")).contains(evil),
                 "the child inherited an attacker-writable QT_PLUGIN_PATH entry");

        // Belt and braces on the whole dump: no search-list variable may name the plant.
        for (auto it = got.cbegin(); it != got.cend(); ++it)
            if (it.key().endsWith(QStringLiteral("PATH")) || it.key().startsWith(QStringLiteral("LD_")))
                QVERIFY2(!it.value().contains(evil),
                         qPrintable(QStringLiteral("a search list still names the attacker's "
                                                   "directory: ") + it.key()));
    }

    // Review 3's F2: the backup name is second-granular and a colliding one used to be REMOVED,
    // so two backup-producing operations in the same wall-clock second destroyed the first
    // backup - which can be a user's only copy of their wallet. The "nothing is deleted, always
    // renamed aside" guarantee is what makes the ungated reset/restore defensible, so it has to
    // be true.
    void testASecondBackupInTheSameSecondNeverDestroysTheFirst()
    {
        const QString storage = writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"victim\"}"));
        // A decoy at exactly the name the next reset will pick (same UTC second).
        const QString decoy = storage + QStringLiteral(".bak-")
            + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss"));
        writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"victim\"}"));
        { QFile f(decoy); f.open(QIODevice::WriteOnly); f.write("THE-ONLY-COPY"); }

        WalletPlugin p;                                  // locked: the forgot-password path
        auto r1 = parseObj(p.resetWallet());
        const QString b1 = r1[QStringLiteral("backup")].toString();
        QVERIFY(!b1.isEmpty());
        QVERIFY2(b1 != decoy, "the backup reused a name that already existed");
        QCOMPARE(slurp(decoy), QString("THE-ONLY-COPY"));   // NOT deleted
        QVERIFY(slurp(b1).contains(QStringLiteral("victim")));

        // A third store in the same second gets a third name, and neither earlier file moves.
        writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"second\"}"));
        auto r2 = parseObj(p.resetWallet());
        const QString b2 = r2[QStringLiteral("backup")].toString();
        QVERIFY(b2 != b1 && b2 != decoy);
        QCOMPARE(slurp(decoy), QString("THE-ONLY-COPY"));
        QVERIFY(slurp(b1).contains(QStringLiteral("victim")));
        QVERIFY(slurp(b2).contains(QStringLiteral("second")));
        QFile::remove(decoy); QFile::remove(b1); QFile::remove(b2);
    }

    // ── The mint bypass (review 1, finding 1) ────────────────────────────────────
    // setSessionPassword's own gate could not fire: clearSessionPassword() is ungated by design,
    // so the attacker emptied m_password first and the conditional `if (!m_password.isEmpty()
    // && !authorize(...))` skipped. moc published the verb, so callModule("medusa_core",
    // "setSessionPassword", ["A"]) was a live remote entry point. The meta-object IS that remote
    // surface, so assert the verb is not on it - a header-only deletion that left a Q_INVOKABLE
    // behind would still be exploitable.
    void testSessionPasswordSetterIsNotOnTheRemoteSurface()
    {
        const QMetaObject& mo = WalletPlugin::staticMetaObject;
        for (int i = 0; i < mo.methodCount(); ++i)
            QVERIFY2(mo.method(i).name() != QByteArray("setSessionPassword"),
                     "setSessionPassword is still callable over callModule");
    }

    // The full observed exploit, as a test: lock the wallet (ungated), then try to establish a
    // session password of the attacker's choosing and use it. Every route must fail, and the
    // user's own password must still work afterwards.
    void testNoVerbMintsASessionPassword()
    {
        useCli(makePasswordCheckingCli(kPw));      // behaves like a real encrypted store
        WalletPlugin p;
        arm(p);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), true);

        p.clearSessionPassword();                  // step 1 of the exploit: ungated, by design
        useCli(makePasswordCheckingCli(kPw));      // arm() restored the caller's CLI; keep this one

        // step 2: install "attacker-chosen". unlock() is the only verb that can set a session
        // password now, and it has to prove the candidate against the store first.
        auto minted = parseObj(p.unlock(QStringLiteral("attacker-chosen")));
        QCOMPARE(minted[QStringLiteral("reason")].toString(), QString("unauthorized"));
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);

        // step 3: the minted password must open nothing.
        for (const auto& v : gatedVerbs()) {
            const auto r = parseObj(v.second(p, QStringLiteral("attacker-chosen")));
            QVERIFY2(r[QStringLiteral("reason")].toString() == QStringLiteral("locked"),
                     qPrintable(v.first + QStringLiteral(" accepted a minted password")));
        }
        // …and the real user is not locked out by any of this.
        QVERIFY(!parseObj(p.unlock(kPw)).contains(QStringLiteral("error")));
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), true);
    }

    // ── unlock(): oracle hardening ───────────────────────────────────────────────
    void testUnlockWrongGuessKeepsLiveSession()
    {
        useCli(makeFakeCli(R"({"error":"Failed to decrypt wallet storage"})", 1));
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.unlock(QStringLiteral("wrong")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("unauthorized"));
        // The established session survived the guess (it used to be cleared, a free DoS)…
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), true);
        // …and it is still the ORIGINAL password, not the guess: kPw passes the gate, the guess
        // does not.
        QVERIFY(parseObj(p.exportMnemonic(QStringLiteral("wrong")))[QStringLiteral("reason")]
                    .toString() == QStringLiteral("unauthorized"));
        QVERIFY(parseObj(p.exportMnemonic(kPw))[QStringLiteral("reason")].toString()
                != QStringLiteral("unauthorized"));
    }

    void testUnlockBacksOffAfterRepeatedFailures()
    {
        useCli(makeFakeCli(R"({"error":"Failed to decrypt wallet storage"})", 1));
        armStore();
        WalletPlugin p;
        for (int i = 0; i < 4; ++i)
            QCOMPARE(parseObj(p.unlock(QStringLiteral("wrong")))[QStringLiteral("reason")].toString(),
                     QString("unauthorized"));
        // The 5th attempt is refused without even consulting the CLI.
        auto r = parseObj(p.unlock(QStringLiteral("wrong")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("rate-limited"));
        QVERIFY(r[QStringLiteral("retryAfterMs")].toInt() > 0);
    }

    // ── Idle auto-lock ───────────────────────────────────────────────────────────
    void testIdleAutoLockClearsTheSession()
    {
        qputenv("MEDUSA_IDLE_LOCK_MS", "200");
        WalletPlugin p;
        arm(p);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), true);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("autoLockMs")].toInt(), 200);
        // The session used to last until the module process exited - days on a desktop.
        QTRY_VERIFY_WITH_TIMEOUT(
            !parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), 5000);
        // And a gated verb fails closed once it has lapsed.
        QCOMPARE(parseObj(p.exportMnemonic(kPw))[QStringLiteral("reason")].toString(),
                 QString("locked"));
    }

    // The UI polls listAccounts every 10 s. If any CLI call counted as activity, the idle lock
    // would be re-armed forever and would never fire on a real install - so only a PRIVILEGED
    // action (a passed gate, an unlock) counts.
    void testIdleAutoLockIsNotHeldOpenByBackgroundPolling()
    {
        qputenv("MEDUSA_IDLE_LOCK_MS", "300");
        useCli(makeFakeCli(R"([{"id":"Public/abc","type":"public"}])"));
        WalletPlugin p;
        arm(p);
        for (int i = 0; i < 6; ++i) {      // ~600 ms of the UI's status polling
            p.listAccounts();
            QTest::qWait(100);
        }
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
    }

    // createEncryptedWallet calls exportMnemonic internally to hand the user their phrase. That
    // is a gated verb now, so this pins the internal call site: a wallet created without a
    // recovery phrase would be a silent, unrecoverable disaster.
    void testCreateEncryptedWalletStillReturnsTheMnemonic()
    {
        useCli(makeStoreWritingCli("Generated new account with account_id Public/abc123 at path 0"));
        WalletPlugin p;                       // fresh install: no store on disk
        auto r = parseObj(p.createEncryptedWallet(QStringLiteral("brand new pw")));
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        QCOMPARE(r[QStringLiteral("id")].toString(), QString("Public/abc123"));
        QVERIFY(!r[QStringLiteral("mnemonic")].toString().isEmpty());
        // …and it leaves the wallet LOCKED (invariant A): sealing a store with a password the
        // caller chose is not evidence about who the caller is, so no session comes out of it.
        // The store is real and encrypted; the user unlocks with what they just typed.
        QCOMPARE(r[QStringLiteral("locked")].toBool(), true);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        QCOMPARE(parseObj(p.getWalletState())[QStringLiteral("encrypted")].toBool(), true);
    }

    // A wallet CLI that reports success and writes nothing used to leave the module claiming a
    // wallet had been created. There is no wallet; say so.
    void testCreateEncryptedWalletFailsWhenTheCliWritesNoStore()
    {
        useCli(makeFakeCli("Generated new account with account_id Public/abc123 at path 0"));
        WalletPlugin p;
        auto r = parseObj(p.createEncryptedWallet(QStringLiteral("brand new pw")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("not-created"));
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
    }

    // ── Legacy PLAINTEXT stores (review 1, findings 3 and 4) ─────────────────────
    // Two facts about the engine drive all of this. (1) The CLI ignores the password when the
    // store carries no crypto envelope, so it opens a plaintext store with ANY password.
    // (2) It re-serialises and SEALS the whole store on any write once the password is
    // non-empty. Together those made one ungated call enough to take a legacy wallet away from
    // its owner - and a plaintext store holds no mnemonic, so there was no phrase to restore
    // from either.

    QString writePlaintextStore()
    {
        return writeStorage(QStringLiteral("{\"accounts\":[{\"id\":\"Public/victim\"}]}"));
    }

    // Finding 3, exactly as it was run: createEncryptedWallet against a victim's plaintext store.
    void testCreateEncryptedWalletRefusesToReSealAnExistingStore()
    {
        useCli(makeFakeCli("Generated new account with account_id Public/attacker at path 0"));
        const QString storage = writePlaintextStore();
        const QString before  = slurp(storage);

        WalletPlugin p;
        auto r = parseObj(p.createEncryptedWallet(QStringLiteral("attacker-pw")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("wallet-not-encrypted"));
        QCOMPARE(slurp(storage), before);     // the victim's store was not touched
        // …and no session was established off the back of it.
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        QFile::remove(storage);
    }

    void testCreateEncryptedWalletRefusesWhenAnEncryptedStoreExists()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        const QString storage = writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"x\"}"));
        WalletPlugin p;
        auto r = parseObj(p.createEncryptedWallet(QStringLiteral("attacker-pw")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("wallet-exists"));
        QFile::remove(storage);
    }

    // ── INVARIANT C ─────────────────────────────────────────────────────────────
    // Round 1 finding 4 was "the gate is a no-op on a plaintext store". Round 2 made the gate
    // refuse everything there, and that produced a user with funds and no working button. Round 3
    // made the gate PASS there, which is how review 3 stole an encrypted wallet's seed: the
    // "is it plaintext" test is a 256-byte guess about a file the attacker owns, and a store that
    // is still fully encrypted can be made to answer yes (see the re-wrap PoC below).
    //
    // Round 4 takes the third option, which is the only one that is a rule rather than a guess:
    // THE GATE IS A FUNCTION OF THE SECRET ALONE. On a plaintext store there is no secret, so the
    // gate refuses - but it refuses with a reason that carries the route out, and the route
    // (migration, which needs no session) is open, ungated, and lands the user on a wallet where
    // everything works. That is what this pins: refused, told why, and NOT stranded.
    void testPlaintextStoreRefusesTheGateAndRoutesTheOwnerToMigration()
    {
        const QString storage = writePlaintextStore();
        useCli(makePasswordCheckingCli(kPw));

        WalletPlugin p;
        // There is nothing to unlock, and no candidate could be verified against a store the CLI
        // opens with anything: unlock must not become the mint.
        auto u = parseObj(p.unlock(QStringLiteral("anything at all")));
        QCOMPARE(u[QStringLiteral("reason")].toString(), QString("unencrypted"));
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        // The user is told the truth, in a field and in words.
        auto sec = parseObj(p.getSecurityState());
        QCOMPARE(sec[QStringLiteral("unencrypted")].toBool(), true);
        QCOMPARE(sec[QStringLiteral("protected")].toBool(), false);
        QVERIFY(sec[QStringLiteral("warning")].toString().contains(QStringLiteral("NOT encrypted")));
        QCOMPARE(parseObj(p.getWalletState())[QStringLiteral("encrypted")].toBool(), false);

        // Every gated verb refuses, with the ONE reason that routes to the fix - not "locked"
        // (which sends the UI to a password prompt for a password that cannot exist) and not
        // "unauthorized" (which tells the user their own password is wrong).
        useCli(makeFakeCli(R"({"ok":true})"));
        for (const auto& v : gatedVerbs()) {
            const auto r = parseObj(v.second(p, QString()));
            QVERIFY2(r[QStringLiteral("reason")].toString() == QStringLiteral("unencrypted"),
                     qPrintable(v.first + QStringLiteral(" gave a plaintext wallet's owner the "
                                                         "wrong route: ")
                                + r[QStringLiteral("reason")].toString()));
            QVERIFY2(r[QStringLiteral("error")].toString().contains(QStringLiteral("set a password")),
                     qPrintable(v.first + QStringLiteral(" refuses without naming the way out")));
        }

        // NOT STRANDED, part 1: everything that does not need a secret still works, so the wallet
        // is readable and its escapes are reachable while the user decides.
        useCli(makeFakeCli(R"([{"id":"Public/victim","type":"public"}])"));
        QVERIFY(!parseArr(p.listAccounts()).isEmpty());
        QVERIFY(!parseObj(p.getWalletState()).isEmpty());
        QVERIFY(!parseObj(p.getSequencerStatus()).isEmpty());

        // NOT STRANDED, part 2: the route out actually works, end to end, from exactly this
        // state - migrate (no session needed), unlock, and every gated verb is reachable again.
        useCli(makeSealingCli(storage));
        auto mig = parseObj(p.encryptPlaintextWallet(QStringLiteral("my new password")));
        QCOMPARE(mig[QStringLiteral("ok")].toBool(), true);
        QVERIFY(!parseObj(p.unlock(QStringLiteral("my new password")))
                     .contains(QStringLiteral("error")));
        useCli(makeFakeCli(R"({"ok":true})"));
        for (const auto& v : gatedVerbs()) {
            const auto r = parseObj(v.second(p, QStringLiteral("my new password")));
            const QString reason = r[QStringLiteral("reason")].toString();
            QVERIFY2(reason != QStringLiteral("unencrypted") && reason != QStringLiteral("locked")
                         && reason != QStringLiteral("unauthorized"),
                     qPrintable(v.first + QStringLiteral(" still refuses after the migration")));
            QTest::qWait(20);
        }
        QFile::remove(mig[QStringLiteral("backup")].toString());
        QFile::remove(storage);
    }

    // ══ THE ROUND-3 CRITICAL, AS THE REVIEWER RAN IT ═══════════════════════════════════════════
    // A store that is still FULLY ENCRYPTED, re-serialised by a co-resident module so that its
    // "kdf"/"ct" markers fall past the 256 bytes storageIsPlaintext() reads. The CLI parses JSON
    // and decrypts it regardless of key order; the module's header check does not. Round 3's
    // authorize() short-circuited on that check BEFORE comparing anything, so:
    //     exportMnemonic("i-do-not-know-the-password")  ->  the victim's 24 words.
    // The observed exploit, verbatim, is now a test.
    QString reWrappedEncryptedStore()
    {
        // Valid JSON, genuinely the encrypted envelope, markers pushed past byte 256.
        return QStringLiteral("{\"note\":\"") + QString(400, QLatin1Char('x'))
             + QStringLiteral("\",\"kdf\":\"argon2id\",\"ct\":\"the-victims-sealed-keys\"}");
    }

    void testReWrappedStoreCannotDefeatTheGate()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;
        arm(p);                                   // the victim is unlocked and working
        useCli(makeFakeCli(R"({"ok":true,"output":"THE-VICTIMS-24-WORD-SEED"})"));

        const QString storage = writeStorage(reWrappedEncryptedStore());
        // The misread is real - the module's 256-byte header check does say "plaintext" - and it
        // is allowed to stay visible in the REPORT, because a report cannot let anyone in.
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("unencrypted")].toBool(), true);

        // What it must not do any more is decide the gate. Every gated verb, wrong password.
        for (const auto& v : gatedVerbs()) {
            const auto r = parseObj(v.second(p, QStringLiteral("i-do-not-know-the-password")));
            QVERIFY2(r[QStringLiteral("reason")].toString() == QStringLiteral("unauthorized"),
                     qPrintable(v.first + QStringLiteral(" was opened by a re-wrapped store")));
            QVERIFY2(!r[QStringLiteral("mnemonic")].toString().contains(QStringLiteral("SEED")),
                     qPrintable(v.first + QStringLiteral(" leaked the seed")));
        }
        // …and the owner, who knows the password, is not locked out by the same file.
        QVERIFY(parseObj(p.exportMnemonic(kPw))[QStringLiteral("reason")].toString().isEmpty());
        QFile::remove(storage);
    }

    // The engine-independent variant of the same attack: every legitimate CLI write truncates
    // storage.json to 0 bytes for an instant, and a 0-byte (or unreadable) file also reads as
    // "plaintext". Round 3's gate passed for ANY password during that window. Plus the shapes a
    // co-resident module can simply write. The gate must ignore all of it: with a live session the
    // wrong password never passes and the right one always does, whatever is on disk.
    void testGateIsAFunctionOfTheSecretNotOfTheStoreOnDisk()
    {
        const QString home = qEnvironmentVariable("LEE_WALLET_HOME_DIR");
        const QString storage = home + QStringLiteral("/storage.json");

        QVector<QPair<QString, QString>> shapes{
            { QStringLiteral("encrypted"),   QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"x\"}") },
            { QStringLiteral("re-wrapped"),  reWrappedEncryptedStore() },
            { QStringLiteral("plaintext"),   QStringLiteral("{\"accounts\":[{\"id\":\"Public/v\"}]}") },
            { QStringLiteral("zero-byte"),   QString() },
            { QStringLiteral("garbage"),     QStringLiteral("not json at all") },
        };
        for (const auto& shape : shapes) {
            useCli(makeFakeCli(R"({"ok":true})"));
            WalletPlugin p;
            arm(p);                                  // a live session over an ENCRYPTED store
            useCli(makeFakeCli(R"({"ok":true})"));
            writeStorage(shape.second);              // …swapped underneath it
            QCOMPARE(parseObj(p.exportMnemonic(QStringLiteral("wrong")))
                         [QStringLiteral("reason")].toString(), QString("unauthorized"));
            QVERIFY2(parseObj(p.exportMnemonic(kPw))[QStringLiteral("reason")].toString().isEmpty(),
                     qPrintable(QStringLiteral("the owner was stranded by a ") + shape.first
                                + QStringLiteral(" store")));
            QFile::remove(storage);
        }

        // …and the same for a store that exists but cannot be READ (chmod 000), which
        // storageIsPlaintext() also reports as plaintext.
        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;
        arm(p);
        useCli(makeFakeCli(R"({"ok":true})"));
        writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"x\"}"));
        QFile::setPermissions(storage, QFile::Permissions());
        QCOMPARE(parseObj(p.exportMnemonic(QStringLiteral("wrong")))[QStringLiteral("reason")]
                     .toString(), QString("unauthorized"));
        QFile::setPermissions(storage, QFile::ReadOwner | QFile::WriteOwner);
        QFile::remove(storage);
    }

    // The same coerced reading used to switch OFF the checks in the two recovery verbs, because
    // they asked storeCanProvePassword() - a fact about the file - whether to enforce at all.
    // Both decide on the in-memory session now.
    void testRecoveryVerbsEnforceTheSessionWhateverTheStoreHeaderSays()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;
        arm(p);
        useCli(makeFakeCli(R"({"ok":true})"));
        const QString storage = writeStorage(reWrappedEncryptedStore());
        const QString before  = slurp(storage);

        QCOMPARE(parseObj(p.resetWallet(QStringLiteral("wrong")))[QStringLiteral("reason")]
                     .toString(), QString("unauthorized"));
        QCOMPARE(parseObj(p.resetWallet())[QStringLiteral("reason")].toString(),
                 QString("unauthorized"));
        QCOMPARE(parseObj(p.restoreWallet(QStringLiteral("attacker phrase words here"),
                                          QStringLiteral("P"), 2, QStringLiteral("wrong")))
                     [QStringLiteral("reason")].toString(), QString("unauthorized"));
        QCOMPARE(slurp(storage), before);      // the victim's store is untouched

        // And the last verb that rewrites a store on the strength of that reading: a live session
        // means the store was encrypted when it was opened, so a "migration" here is a lie.
        QCOMPARE(parseObj(p.encryptPlaintextWallet(QStringLiteral("attacker-pw")))
                     [QStringLiteral("reason")].toString(), QString("already-encrypted"));
        QCOMPARE(slurp(storage), before);
        QFile::remove(storage);
    }

    // The gate reads the SECRET, not the store, so a downgrade underneath a live session changes
    // nothing about who may act - but it must still change what the user is TOLD, because the
    // store on disk really is unprotected now.
    void testStoreDowngradedUnderALiveSessionIsReportedUnprotected()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;
        arm(p);
        useCli(makeFakeCli(R"({"ok":true})"));
        QVERIFY(parseObj(p.exportMnemonic(kPw))[QStringLiteral("reason")].toString().isEmpty());
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("protected")].toBool(), true);

        const QString storage = writePlaintextStore();      // swapped underneath the session
        auto sec = parseObj(p.getSecurityState());
        QCOMPARE(sec[QStringLiteral("unencrypted")].toBool(), true);
        QCOMPARE(sec[QStringLiteral("protected")].toBool(), false);   // the claim is withdrawn…
        QVERIFY(!sec[QStringLiteral("warning")].toString().isEmpty());
        QCOMPARE(parseObj(p.getWalletState())[QStringLiteral("encrypted")].toBool(), false);
        // …the owner is not stranded by the downgrade…
        QVERIFY(parseObj(p.exportMnemonic(kPw))[QStringLiteral("reason")].toString().isEmpty());
        // …and a caller who does not know the password gains nothing from it. THIS assertion is
        // the one this test was missing in round 3, and it is the whole exploit.
        QCOMPARE(parseObj(p.exportMnemonic(QStringLiteral("i-do-not-know-the-password")))
                     [QStringLiteral("reason")].toString(), QString("unauthorized"));
        QFile::remove(storage);
    }

    // …and the plaintext user must not be stuck there. Migration is its own verb, it keeps a
    // copy of what it re-seals, and it is honest that there is no recovery phrase.
    void testPlaintextWalletCanMigrateToEncrypted()
    {
        const QString storage = writePlaintextStore();
        const QString before  = slurp(storage);
        useCli(makeSealingCli(storage));

        WalletPlugin p;
        auto r = parseObj(p.encryptPlaintextWallet(QStringLiteral("my new password")));
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        QCOMPARE(r[QStringLiteral("migrated")].toBool(), true);
        // The store it re-sealed survives verbatim: a migration nobody asked for is undoable.
        const QString backup = r[QStringLiteral("backup")].toString();
        QVERIFY(!backup.isEmpty());
        QCOMPARE(slurp(backup), before);
        // The wallet is encrypted now…
        QCOMPARE(parseObj(p.getWalletState())[QStringLiteral("encrypted")].toBool(), true);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("unencrypted")].toBool(), false);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("protected")].toBool(), true);
        // …and NO session came out of it (invariant A). This verb was review 2's C1: one ungated
        // call re-sealed a legacy wallet under an attacker's password AND handed the attacker a
        // live session on it, so 12/12 gated verbs then accepted a password the owner never
        // chose. Sealing grants nothing now; the owner unlocks with what they typed.
        QCOMPARE(r[QStringLiteral("locked")].toBool(), true);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        for (const auto& v : gatedVerbs()) {
            const auto g = parseObj(v.second(p, QStringLiteral("my new password")));
            QVERIFY2(g[QStringLiteral("reason")].toString() == QStringLiteral("locked"),
                     qPrintable(v.first + QStringLiteral(" accepted a seal-minted password")));
        }
        // The owner unlocks and everything works.
        QVERIFY(!parseObj(p.unlock(QStringLiteral("my new password")))
                     .contains(QStringLiteral("error")));
        QVERIFY(parseObj(p.exportMnemonic(QStringLiteral("my new password")))
                    [QStringLiteral("reason")].toString().isEmpty());
        QCOMPARE(parseObj(p.exportMnemonic(QStringLiteral("wrong")))[QStringLiteral("reason")]
                     .toString(), QString("unauthorized"));
        // It does not pretend a phrase exists: a plaintext store never held one.
        QVERIFY(r[QStringLiteral("mnemonic")].toString().isEmpty());
        QVERIFY(!r[QStringLiteral("note")].toString().isEmpty());
        QFile::remove(backup);
        QFile::remove(storage);
    }

    // MIGRATION MUST NOT LOCK ITS OWNER OUT. A plaintext store has no recovery phrase, so if the
    // sealed store will not open with the password that sealed it there is nothing to fall back
    // on but the copy taken first - and the copy is only worth anything if the module puts it
    // back. An engine that seals with a key the CLI cannot re-derive is exactly this shape.
    void testFailedMigrationPutsTheUnencryptedWalletBack()
    {
        const QString storage = writePlaintextStore();
        const QString before  = slurp(storage);
        // Seals on the first call (the migration), then refuses to open it on any later one.
        const QString cli = m_tmp.path() + QStringLiteral("/badseal_wallet.sh");
        {
            QFile f(cli);
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            f.write("#!/bin/sh\nread pw\n");
            f.write(QString("if [ -f '%1.sealed' ]; then\n"
                            "  echo '{\"error\":\"Failed to decrypt wallet storage\"}'\n"
                            "  exit 1\n"
                            "fi\n").arg(cli).toUtf8());
            f.write(QString(": > '%1.sealed'\n").arg(cli).toUtf8());
            f.write(QString("printf '{\"kdf\":\"argon2id\",\"ct\":\"unopenable\"}' > '%1'\n")
                        .arg(storage).toUtf8());
            f.write("echo 'Generated new account with account_id Public/fresh at path 0'\n");
            f.close();
            QFile::setPermissions(cli, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
        QFile::remove(cli + QStringLiteral(".sealed"));
        useCli(cli);

        WalletPlugin p;
        auto r = parseObj(p.encryptPlaintextWallet(QStringLiteral("my new password")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("not-encrypted"));
        // The wallet the user started with is back, byte for byte, and still usable.
        QCOMPARE(slurp(storage), before);
        QCOMPARE(parseObj(p.getWalletState())[QStringLiteral("encrypted")].toBool(), false);
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        QFile::remove(storage);
        for (const QString& f : QDir(qEnvironmentVariable("LEE_WALLET_HOME_DIR"))
                                    .entryList({QStringLiteral("storage.json.plain-*")}, QDir::Files))
            QFile::remove(QDir(qEnvironmentVariable("LEE_WALLET_HOME_DIR")).filePath(f));
    }

    // A plaintext wallet must not become a trap. Recovery is conditionally gated on a session
    // that CAN be proved, and on a plaintext store none can, so reset and restore stay open -
    // exactly as they are for the user who forgot their password.
    void testPlaintextStoreStillAllowsReset()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        const QString storage = writePlaintextStore();
        WalletPlugin p;
        auto r = parseObj(p.resetWallet());
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);
        QVERIFY(!QFile::exists(storage));                       // moved aside…
        const QString backup = r[QStringLiteral("backup")].toString();
        QVERIFY(QFile::exists(backup));                         // …never destroyed
        QFile::remove(backup);
    }

    // "The user must not be silently told they are protected when they are not." An engine built
    // without encrypted storage writes a plaintext store and exits 0; reporting {ok} there would
    // leave the user believing in a password while every gated verb refuses it.
    void testCreateEncryptedWalletFailsLoudlyIfTheStoreEndsUpPlaintext()
    {
        const QString storage = qEnvironmentVariable("LEE_WALLET_HOME_DIR")
                              + QStringLiteral("/storage.json");
        QDir().mkpath(qEnvironmentVariable("LEE_WALLET_HOME_DIR"));
        const QString cli = m_tmp.path() + QStringLiteral("/nocrypt_wallet.sh");
        {
            QFile f(cli);
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            f.write("#!/bin/sh\nread pw\n");
            f.write(QString("printf '{\"accounts\":[]}' > '%1'\n").arg(storage).toUtf8());
            f.write("echo 'Generated new account with account_id Public/abc at path 0'\n");
            f.close();
            QFile::setPermissions(cli, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
        useCli(cli);

        WalletPlugin p;
        auto r = parseObj(p.createEncryptedWallet(QStringLiteral("brand new pw")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("not-encrypted"));
        QCOMPARE(parseObj(p.getSecurityState())[QStringLiteral("hasPassword")].toBool(), false);
        QFile::remove(storage);
    }

    void testEncryptPlaintextWalletRefusesAnEncryptedStore()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        const QString storage = writeStorage(QStringLiteral("{\"kdf\":\"argon2id\",\"ct\":\"x\"}"));
        WalletPlugin p;
        auto r = parseObj(p.encryptPlaintextWallet(QStringLiteral("pw")));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("already-encrypted"));
        QFile::remove(storage);
    }

    // ── The zone-repointing chain (review 2, F1) ─────────────────────────────────
    // Every step of connectRequest → approveConnect → requestZone → approveZone is callable by a
    // co-resident module with no user present, and the last one used to switch the wallet to the
    // sequencer the caller named. A sequencer sees every public transaction, can censor them and
    // supplies every balance the UI shows.
    void testZoneApprovalChainCannotRepointALockedWallet()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;                       // locked, nobody at the keyboard
        const QString zoneBefore = parseObj(p.getNetwork())[QStringLiteral("network")].toString();

        const QString sid = connectAndApprove(p, {QStringLiteral("zone")},
                                              {QStringLiteral("Public/a")});
        QVERIFY(!sid.isEmpty());              // the connect half is deliberately still open
        auto rz = parseObj(p.requestZone(sid, QStringLiteral(
            "{\"sequencer\":\"http://attacker.example:3080\",\"label\":\"free money\"}")));
        const QString reqId = rz[QStringLiteral("requestId")].toString();
        QVERIFY(!reqId.isEmpty());

        auto ap = parseObj(p.approveZone(reqId));
        QCOMPARE(ap[QStringLiteral("reason")].toString(), QString("locked"));
        // The request is untouched (still approvable by the real user) and the wallet still
        // points where it did.
        QCOMPARE(parseObj(p.actionStatus(reqId))[QStringLiteral("status")].toString(),
                 QString("pending"));
        QCOMPARE(parseObj(p.getNetwork())[QStringLiteral("network")].toString(), zoneBefore);
        QVERIFY2(!p.getZones().contains(QStringLiteral("attacker.example")),
                 "the attacker's sequencer was added as a zone");
    }

    // …and an unlocked wallet is no better for a caller that does not know the password, while
    // the real user's approval still goes through and switches the zone.
    void testZoneApprovalNeedsThePasswordAndStillWorksWithIt()
    {
        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;
        arm(p);
        useCli(makeFakeCli(R"({"ok":true})"));

        const QString sid = connectAndApprove(p, {QStringLiteral("zone")},
                                              {QStringLiteral("Public/a")});
        auto rz = parseObj(p.requestZone(sid, QStringLiteral(
            "{\"sequencer\":\"http://zone.example:3080\",\"label\":\"Zone Example\"}")));
        const QString reqId = rz[QStringLiteral("requestId")].toString();

        QCOMPARE(parseObj(p.approveZone(reqId, QStringLiteral("not the password")))
                     [QStringLiteral("reason")].toString(), QString("unauthorized"));
        QCOMPARE(parseObj(p.actionStatus(reqId))[QStringLiteral("status")].toString(),
                 QString("pending"));

        auto ok = parseObj(p.approveZone(reqId, kPw));
        QCOMPARE(ok[QStringLiteral("status")].toString(), QString("approved"));
        QVERIFY(!ok[QStringLiteral("zoneId")].toString().isEmpty());
        QCOMPARE(parseObj(p.getNetwork())[QStringLiteral("network")].toString(),
                 ok[QStringLiteral("zoneId")].toString());
    }

    // ── importKey: the signing key must not reach argv ────────────────────────────
    void testImportKeySendsKeyOnStdinNotArgv()
    {
        const QString cli = makeRecordingCli("Imported account with account_id Public/abc");
        useCli(cli);

        armStore();
        WalletPlugin p;
        QVERIFY(!parseObj(p.unlock(QStringLiteral("pw-line-1"))).contains(QStringLiteral("error")));
        const QString key = QStringLiteral("10a26a9aec7d34b82364eeae45c5294d"
                                           "bb0a764b000b94eeb9b58511dc487c4d");
        auto r = parseObj(p.importKey(key, QString()));   // no label: keeps this the last command
        QCOMPARE(r[QStringLiteral("ok")].toBool(), true);

        const QString argv = slurp(cli + QStringLiteral(".argv"));
        // /proc/<pid>/cmdline is world-readable and the wrapper re-execs argv verbatim, so the
        // key on the command line was readable by any local uid for the length of the import.
        QVERIFY2(!argv.contains(key), "the private key is still on argv");
        QVERIFY(!argv.contains(QStringLiteral("--private-key")));
        QCOMPARE(argv, QString("account\nimport\npublic\n"));

        // It travels on stdin instead, on line 2 - the CLI reads the password as line 1.
        const QStringList stdinLines = slurp(cli + QStringLiteral(".stdin")).split(QLatin1Char('\n'));
        QCOMPARE(stdinLines.value(0), QString("pw-line-1"));
        QCOMPARE(stdinLines.value(1), key);
    }
};

QTEST_MAIN(TestWalletPlugin)
#include "test_wallet_plugin.moc"

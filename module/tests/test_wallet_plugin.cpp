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
#include <QElapsedTimer>

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

// ── The two spec strings, written out here rather than built ───────────────────────────────
// Every value-moving verb takes ONE value argument and (on the privacy side) ONE recipient
// argument, because the Logos bridge marshals at most 5 (qt_provider_object.cpp:20-34) and the
// session password has to stay last. Spelling the encodings out literally in the suite means a
// silent change to either parser fails a test instead of quietly re-encoding a spend.
static const QString kTokenValue =
    QStringLiteral(R"({"asset":"token","definitionId":"def1","amount":"1"})");
static const QString kForeignRecipient =
    QStringLiteral(R"({"npk":"npk","vpk":"vpk","identifier":"ident"})");

// The deployed `medusa_faucet` program id, written out HERE independently of the module so a
// silent edit to the module's constant fails a test instead of quietly repointing every claim.
// It is the risc0 ImageID of the audited 437 884-byte guest, and therefore the same on every
// zone: a LEZ program id is computed from the ELF alone, with no channel, deployer or endpoint
// mixed in. It was deployed under exactly this id to seq-testnet.paradox.computer (block 975)
// and testnet.lez.logos.co (block 44810) on 2026-07-31.
static const QString kFaucetId =
    QStringLiteral("523320bdfff97cdbec1f01fdb5de9c37b4555abb7585cd123d77e9d09756e571");

// ── The per-zone faucet token table, transcribed independently of the module ───────────────
// Same reasoning as kFaucetId: these are chain facts (definitions minted and treasuries funded
// and verified on 2026-07-31), so a silent edit to kFaucetZones has to fail a test rather than
// quietly repoint every claim at accounts that do not exist. The wallet's DEFAULT zone is
// "paradox-clearnet", which is why these are the ids the untouched tests below see.
static const QStringList kParadoxDefs{
    QStringLiteral("5YEhWdY2edtRFkCruXjtnFH5F62VkCiCxXmNAvHuVkEY"),   // GOLD
    QStringLiteral("HUDERmRqyX6swMnuk9FT5vmqNbcdLNbVxDRtLEdzsMXk"),   // SILV
    QStringLiteral("3zS3bGdToZcqPU9jBZC8c1aK9MQvpekse9EJ52nD1wiM") }; // BRNZ
static const QStringList kParadoxTreasuries{
    QStringLiteral("A9NwZksDYPzZzpdnbHmJkcEwgHvbGmmYNsYV9rHGoxAF"),
    QStringLiteral("5iG2BTUhWCmgviBw54ZMtr3qjSMyLfPz7pMNAwvk6kiQ"),
    QStringLiteral("89MWMvGchyEVq4FZFQPsXS747LQjLe4L9ev4hXMBY8PK") };
static const QStringList kLogosDefs{
    QStringLiteral("7ZZGE941fzSGCAfxxdkPWQszSspBhZEcjHUkLqWrrnz6"),   // GOLD
    QStringLiteral("CfuvpaUhbxEzWd6ZtLDiKWVg5DZLiYj14Q8HgtDUwuS6"),   // SILV
    QStringLiteral("EEMUsdWL1WxrQBi1SmNFUKVcMUjgVcky12NRv2BjBuxp") }; // BRNZ

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

    // ── On-chain faucet stand-ins ───────────────────────────────────────────────
    // A fake medusa-faucet-client. `programId` is what its OFFLINE `info` subcommand reports,
    // which is the whole point: a test can hand the wallet a guest binary that is not the
    // deployed program and watch it refuse. Records argv and the wallet home it was given, so a
    // test can prove what was claimed, for whom, and out of which wallet.
    QString makeFaucetClient(const QString& programId,
                             const QString& claimOut = QStringLiteral("{\"ok\":true}"),
                             int claimCode = 0)
    {
        const QString path = m_tmp.path() + QStringLiteral("/fake_faucet_client.sh");
        // The temp dir outlives one test, so a previous test's transcript has to go: "the
        // client never ran" is asserted by the ABSENCE of these files, and a stale one would
        // make that assertion fail (or, worse, pass for the wrong reason).
        QFile::remove(path + QStringLiteral(".argv"));
        QFile::remove(path + QStringLiteral(".home"));
        QString body = QStringLiteral("#!/bin/sh\n");
        body += QStringLiteral(": > '%1.argv'\nfor a in \"$@\"; do echo \"$a\" >> '%1.argv'; done\n")
                    .arg(path);
        body += QStringLiteral("printf '%s' \"$LEE_WALLET_HOME_DIR\" > '%1.home'\n").arg(path);
        body += QStringLiteral("if [ \"$1\" = info ]; then\n"
                               "  echo '{\"ok\":true,\"binSizeBytes\":1,\"programId\":\"%1\"}'\n"
                               "  exit 0\n"
                               "fi\n").arg(programId);
        body += QStringLiteral("echo '%1'\nexit %2\n").arg(claimOut).arg(claimCode);
        return writeExec(path, body);
    }
    // Something for faucetGuestBin() to find. Its CONTENT is deliberately not risc0 bytecode:
    // the wallet never parses it, it proves the id through the client's `info` instead.
    QString makeFaucetGuestBin()
    {
        const QString path = m_tmp.path() + QStringLiteral("/medusa_faucet.bin");
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("stand-in for the risc0 guest ELF");
        f.close();
        return path;
    }
    // A wallet CLI for the faucet path. The preflight reads this wallet's holdings registry;
    // the CLAIM path may also mint a holding account ("account new public") and record it
    // ("token-registry vault <def> <bare>"). Every invocation is appended to <path>.argv so a
    // test can prove what the module asked the wallet to do, and each minted account gets a
    // distinct id so the recipients are distinct the way the chain requires.
    // NOTE there is no `whitelist` case: the zone's definitions come from the module's own zone
    // table now, and a test that could feed them in through the CLI would be testing a path
    // that no longer exists.
    QString makeTokenCli(const QString& registryJson)
    {
        const QString path = m_tmp.path() + QStringLiteral("/tokens_wallet.sh");
        QString body = QStringLiteral("#!/bin/sh\n");
        body += QStringLiteral("for a in \"$@\"; do echo \"$a\" >> '%1.argv'; done\n"
                               "echo '--' >> '%1.argv'\n").arg(path);
        body += QStringLiteral(
            "case \"$1\" in\n"
            "  token-registry)\n"
            "    if [ \"$2\" = vault ]; then echo '{\"ok\":true}'\n"
            "    else echo '%1'; fi ;;\n"
            "  account)\n"
            "    if [ \"$2\" = new ]; then\n"
            "      n=$(cat '%2.n' 2>/dev/null || echo 0); n=$((n+1)); echo \"$n\" > '%2.n'\n"
            "      echo \"{\\\"ok\\\":true,\\\"output\\\":\\\"Generated new account with \"\\\n"
            "\"account_id Public/minted$n at path 0\\\"}\"\n"
            "    else echo '{\"ok\":true}'; fi ;;\n"
            "  *) echo '{\"ok\":true}' ;;\n"
            "esac\n").arg(registryJson, path);
        return writeExec(path, body);
    }
    // Install a working on-chain faucet: the real program id, a guest binary, and a wallet that
    // already holds a vault for each of the ACTIVE zone's three faucet tokens. The default from
    // which each test below removes exactly one precondition.
    void useWorkingFaucet(const QString& programId = kFaucetId)
    {
        qputenv("MEDUSA_FAUCET_CLIENT", makeFaucetClient(programId).toUtf8());
        qputenv("MEDUSA_FAUCET_BIN",    makeFaucetGuestBin().toUtf8());
        useCli(makeTokenCli(registryWithVaults(kParadoxDefs)));
    }
    // A token-registry reply designating "vault-<n>" as the holding for each definition.
    static QString registryWithVaults(const QStringList& defs,
                                      const QString& owner = QStringLiteral("my-main-account"))
    {
        QStringList quoted, pairs;
        for (int i = 0; i < defs.size(); ++i) {
            quoted << QStringLiteral("\"%1\"").arg(defs.at(i));
            pairs  << QStringLiteral("\"%1\":\"vault-%2\"").arg(defs.at(i)).arg(i + 1);
        }
        // Vaults are keyed BY OWNER: vaults[owner][definition] = holding. The fixture files
        // them all under kOwner, the account these tests claim from, because a holding that
        // belongs to nobody is exactly what the per-owner change removed.
        return QStringLiteral("{\"definitions\":[%1],\"names\":{},\"vaults\":{\"%2\":{%3}}}")
                   .arg(quoted.join(QLatin1Char(',')), owner, pairs.join(QLatin1Char(',')));
    }
    // The vault ids registryWithVaults() hands out, in the same order.
    static QStringList vaultIds(int n)
    {
        QStringList v;
        for (int i = 1; i <= n; ++i) v << QStringLiteral("vault-%1").arg(i);
        return v;
    }
    // Add a user zone and make it active. Returns its id.
    QString useUserZone(WalletPlugin& p, const QString& name = QStringLiteral("My node"))
    {
        const auto add = parseObj(p.addZone(name, QStringLiteral("https://example.invalid:3072/"),
                                            false));
        const QString id = add[QStringLiteral("id")].toString();
        p.setActiveZone(id);
        return id;
    }
    // Wait for a job to leave "running" (the fake client exits at once, but through the event
    // loop). Returns the terminal job object.
    QJsonObject awaitJob(WalletPlugin& p, const QString& jobId, int budgetMs = 4000)
    {
        QJsonObject j;
        QElapsedTimer t;
        t.start();
        do {
            QTest::qWait(25);
            j = parseObj(p.getJob(jobId));
        } while (j[QStringLiteral("state")].toString() == QStringLiteral("running")
                 && t.elapsed() < budgetMs);
        return j;
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
                                        kTokenValue, pw); } },
            { QStringLiteral("startShield"), [](WalletPlugin& p, const QString& pw) {
                return p.startShield(QStringLiteral("Public/a"), QStringLiteral("Private/s1"),
                                     QStringLiteral("1"), pw); } },
            { QStringLiteral("startDeshield"), [](WalletPlugin& p, const QString& pw) {
                return p.startDeshield(QStringLiteral("Private/a"), QStringLiteral("Public/e"),
                                       QStringLiteral("1"), pw); } },
            { QStringLiteral("startPrivateTransfer"), [](WalletPlugin& p, const QString& pw) {
                return p.startPrivateTransfer(QStringLiteral("Private/a"),
                                              QStringLiteral("Private/s2"),
                                              QStringLiteral("1"), pw); } },
            // The foreign-recipient form is the SAME gated verb with a recipient spec in `to`.
            // It kept its own row here because it used to be its own verb (at an arity the
            // bridge could not dispatch), and losing the row would lose the gate coverage.
            { QStringLiteral("startPrivateTransfer(foreign)"), [](WalletPlugin& p, const QString& pw) {
                return p.startPrivateTransfer(QStringLiteral("Private/a"), kForeignRecipient,
                                              QStringLiteral("1"), pw); } },
            { QStringLiteral("consolidateToken"), [](WalletPlugin& p, const QString& pw) {
                return p.consolidateToken(QStringLiteral("Public/a"), QStringLiteral("def1"), pw); } },
            { QStringLiteral("approveAction"), [](WalletPlugin& p, const QString& pw) {
                return p.approveAction(QStringLiteral("req-1"), pw); } },
            // approveZone is approveAction's twin: it repoints the wallet at a sequencer a
            // foreign app named. It was the one no-user-interaction chain left after the first
            // round of gating.
            { QStringLiteral("approveZone"), [](WalletPlugin& p, const QString& pw) {
                return p.approveZone(QStringLiteral("req-1"), pw); } },
            // The on-chain faucet claim signs and broadcasts with the wallet's keys, so it is a
            // spend verb and belongs on this list rather than beside the ungated startFaucet.
            // Listing it here is what subjects it to all six gate tests at once.
            { QStringLiteral("startTokenFaucet"), [](WalletPlugin& p, const QString& pw) {
                return p.startTokenFaucet(QStringLiteral("Public/a"), pw); } },
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
        // The on-chain faucet's two overrides. Cleared per test for the same reason as the CLI:
        // a leftover stand-in from one test must not decide another's verdict, and no test may
        // fall through to a faucet client the developer happens to have installed.
        qunsetenv("MEDUSA_FAUCET_CLIENT");
        qunsetenv("MEDUSA_FAUCET_BIN");
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
        QString path;
        // BOTH names. cliPath() resolves "medusa-wallet" and only falls back to the legacy
        // "wallet" for installs made before the rename, so the safety net that keeps a test
        // which forgot MEDUSA_WALLET_CLI off the developer's real binary has to cover the name
        // that is actually resolved first.
        for (const QString& name : { QStringLiteral("/medusa-wallet"), QStringLiteral("/wallet") }) {
            path = dir + name;
            QFile f(path);
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            f.write("#!/bin/sh\necho '{\"ok\":true}'\n");
            f.close();
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
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

    // ── The on-chain faucet (the deployed medusa_faucet program) ──────────────
    //
    // The program is deployed on both operator zones, but neither has token definitions or
    // funded treasuries on it yet. So every test here is about the wallet behaving well while
    // the on-chain faucet CANNOT PAY OUT, which is the only state it has ever been in - and
    // about it never quietly replacing the client-side faucet that can.

    // The constant is a fact about the ELF, not about a zone. If someone ever "fixes" this by
    // introducing a per-zone id, the wallet would claim from a program that does not exist on
    // the other zone, and this fails.
    void testFaucetProgramIdIsTheSameConstantOnEveryZone()
    {
        WalletPlugin p;
        const auto a = parseObj(p.faucetStatus());
        QCOMPARE(a[QStringLiteral("programId")].toString(), kFaucetId);
        // Switching zones may not switch programs. Both zones the program was deployed to, by
        // their real ids, plus the local sandbox.
        for (const QString& zone : { QStringLiteral("paradox-clearnet"),
                                     QStringLiteral("logos-testnet"),
                                     QStringLiteral("devnet") }) {
            QVERIFY2(!parseObj(p.setActiveZone(zone)).contains(QStringLiteral("error")),
                     qPrintable(QStringLiteral("no such built-in zone: ") + zone));
            QCOMPARE(parseObj(p.faucetStatus())[QStringLiteral("programId")].toString(), kFaucetId);
        }
    }

    // Nothing is installed: the honest answer is "this wallet cannot do that", named precisely
    // enough to act on, and NOT a crash, a silent no-op, or a claim that looks available.
    void testFaucetStatusSaysWhyItIsUnavailableRatherThanFailingOpaquely()
    {
        WalletPlugin p;
        const auto r = parseObj(p.faucetStatus());
        QCOMPARE(r[QStringLiteral("available")].toBool(), false);
        QCOMPARE(r[QStringLiteral("reason")].toString(),  QString("client-missing"));
        QCOMPARE(r[QStringLiteral("clientFound")].toBool(), false);
        QVERIFY2(r[QStringLiteral("message")].toString().length() > 20,
                 "an unavailable faucet must explain itself, not just refuse");
        // Availability is not a guess about funding: the wallet cannot read a treasury balance,
        // and says so rather than implying it checked.
        QCOMPARE(r[QStringLiteral("funded")].toString(), QString("unknown"));
    }

    // THE CONSTANT DOING WORK. medusa-faucet-client recomputes the program id from whatever .bin
    // it is handed, so a swapped guest binary would silently point a signed claim at a different
    // program. The wallet proves the id offline first and refuses.
    void testAGuestBinaryThatIsNotTheDeployedProgramIsRefused()
    {
        useWorkingFaucet(QStringLiteral(
            "dead00000000000000000000000000000000000000000000000000000000beef"));
        WalletPlugin p;
        arm(p);

        const auto st = parseObj(p.faucetStatus());
        QCOMPARE(st[QStringLiteral("available")].toBool(), false);
        QCOMPARE(st[QStringLiteral("reason")].toString(), QString("program-mismatch"));
        QCOMPARE(st[QStringLiteral("verified")].toBool(), false);

        // …and the claim refuses too, WITH THE CORRECT PASSWORD: the gate is not what stops this.
        const auto r = parseObj(p.startTokenFaucet(QStringLiteral("Public/a"), kPw));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("program-mismatch"));
        QVERIFY2(!r.contains(QStringLiteral("jobId")), "a mismatched program was claimed from");
        // Nothing was ever claimed: the client only ever ran its offline `info`.
        const QString argv = slurp(qEnvironmentVariable("MEDUSA_FAUCET_CLIENT") + QStringLiteral(".argv"));
        QVERIFY2(!argv.contains(QStringLiteral("claim")),
                 qPrintable(QStringLiteral("a claim was sent to a mismatched program: ") + argv));
    }

    // ══ THE PER-ZONE CAPABILITY ═══════════════════════════════════════════════════════════
    // The token faucet exists on the two built-in operator zones and NOWHERE else. It is a
    // property of the zone, published on the zone record, so the list the UI renders and the
    // claim that runs cannot disagree.
    void testTheTokenFaucetIsAPropertyOfTheZoneRecord()
    {
        WalletPlugin p;
        const QString userZone = useUserZone(p);

        QHash<QString, bool> flag;
        const QJsonArray zones = parseObj(p.getZones())[QStringLiteral("zones")].toArray();
        for (const QJsonValue& v : zones)
            flag.insert(v.toObject()[QStringLiteral("id")].toString(),
                        v.toObject()[QStringLiteral("tokenFaucet")].toBool());

        // The two zones the program and its funded treasuries are deployed on.
        QCOMPARE(flag.value(QStringLiteral("paradox-clearnet")), true);
        QCOMPARE(flag.value(QStringLiteral("logos-testnet")),    true);
        // The local sandbox is a chain this wallet starts itself: nothing is deployed on it.
        QCOMPARE(flag.value(QStringLiteral("devnet")),   false);
        QCOMPARE(flag.value(QStringLiteral("diaphani")), false);
        // …and a zone the user added is somebody else's sequencer.
        QVERIFY2(flag.contains(userZone), "the user zone was not listed at all");
        QCOMPARE(flag.value(userZone), false);
    }

    // THE RULE THE OWNER STATED, AS BEHAVIOUR: on a user-added zone the wallet must not even
    // TRY the token half. Not "try and fail politely" - the faucet client must never run, since
    // running it there would spawn a helper, probe a program id and produce an install-side
    // complaint about a faucet that zone was never going to have.
    void testAUserAddedZoneNeverAttemptsTheTokenFaucet()
    {
        useWorkingFaucet();                 // a perfectly good install…
        WalletPlugin p;
        arm(p);
        useUserZone(p);                     // …on a zone that has no token faucet

        const auto st = parseObj(p.faucetStatus());
        QCOMPARE(st[QStringLiteral("available")].toBool(), false);
        QCOMPARE(st[QStringLiteral("reason")].toString(), QString("unsupported-zone"));
        QCOMPARE(st[QStringLiteral("tokenFaucetZone")].toBool(), false);
        QVERIFY2(st[QStringLiteral("definitions")].toArray().isEmpty(),
                 "a zone with no token faucet was given token definitions");

        const auto r = parseObj(p.startTokenFaucet(QStringLiteral("Public/a"), kPw));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("unsupported-zone"));
        QVERIFY2(!r.contains(QStringLiteral("jobId")), "a claim was started on a LEZ-only zone");
        // Nothing was launched: not the claim, and not even the offline program-id probe.
        QVERIFY2(!QFile::exists(qEnvironmentVariable("MEDUSA_FAUCET_CLIENT")
                                + QStringLiteral(".argv")),
                 "the faucet client ran on a zone with no token faucet");
    }

    // The capability is derived from the module's own zone table and never read back off the
    // stored zone record. That record is an ordinary user-writable QSettings value (see
    // cliPath()), so a co-resident module can write one - and if the flag were trusted, it
    // would point a signed claim at a sequencer of the attacker's choosing.
    void testAPlantedZoneRecordCannotGrantItselfTheTokenFaucet()
    {
        QSettings s;
        s.setValue(QStringLiteral("medusa-wallet/zones"),
                   QStringLiteral("[{\"id\":\"z-evil\",\"name\":\"Evil\","
                                  "\"url\":\"https://evil.invalid/\",\"tor\":false,"
                                  "\"tokenFaucet\":true,\"faucetTokens\":true}]"));
        s.sync();

        useWorkingFaucet();
        WalletPlugin p;
        arm(p);
        QVERIFY(!parseObj(p.setActiveZone(QStringLiteral("z-evil"))).contains(QStringLiteral("error")));

        const QJsonArray zones = parseObj(p.getZones())[QStringLiteral("zones")].toArray();
        for (const QJsonValue& v : zones)
            if (v.toObject()[QStringLiteral("id")].toString() == QStringLiteral("z-evil"))
                QVERIFY2(!v.toObject()[QStringLiteral("tokenFaucet")].toBool(),
                         "a planted zone record granted itself the token faucet");

        const auto r = parseObj(p.startTokenFaucet(QStringLiteral("Public/a"), kPw));
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("unsupported-zone"));
        QVERIFY(!r.contains(QStringLiteral("jobId")));
    }

    // The definitions are a fact about the ZONE, held in the module's table. They used to be
    // read from faucet_tokens-<zone>.json in the treasury directory, which is operator state
    // outside the install: deleting it emptied the list with no error anywhere, and that is
    // half of the reported bug (LEZ kept arriving, tokens silently stopped).
    void testTheZonesTokensComeFromTheZoneTableAndNotFromLocalFiles()
    {
        // A wallet CLI that answers EVERYTHING with {"ok":true}: it has no whitelist to give.
        useCli(makeFakeCli(R"({"ok":true})"));
        WalletPlugin p;

        auto defsOf = [&]() {
            QStringList out;
            const QJsonArray a = QJsonDocument::fromJson(p.getWhitelist().toUtf8()).array();
            for (const QJsonValue& v : a) out << v.toObject()[QStringLiteral("def")].toString();
            return out;
        };
        p.setActiveZone(QStringLiteral("paradox-clearnet"));
        QCOMPARE(defsOf(), kParadoxDefs);
        // Per-zone, and NOT the same list twice: the definitions are accounts on one chain.
        p.setActiveZone(QStringLiteral("logos-testnet"));
        QCOMPARE(defsOf(), kLogosDefs);
        // A zone with no token faucet offers no curated tokens at all.
        p.setActiveZone(QStringLiteral("devnet"));
        QCOMPARE(defsOf(), QStringList());
    }

    // A wallet that has never held a faucet token is the NORMAL state after a reset, and it
    // used to be a dead end: the preflight refused with "use the standard faucet once (it
    // creates one)", advice that stopped being true the moment the client-side token drop was
    // removed. The on-chain program accepts an uninitialized recipient, so the claim path mints
    // one per definition and records it, and the claim goes ahead.
    void testAWalletWithNoHoldingHasOneCreatedForItRatherThanBeingRefused()
    {
        qputenv("MEDUSA_FAUCET_CLIENT", makeFaucetClient(kFaucetId).toUtf8());
        qputenv("MEDUSA_FAUCET_BIN",    makeFaucetGuestBin().toUtf8());
        useCli(makeTokenCli(QStringLiteral("{\"definitions\":[],\"names\":{},\"vaults\":{}}")));

        WalletPlugin p;
        arm(p);
        // Status stays honest AND stays read-only: it reports the faucet as available (it is),
        // and it must not mint anything, because the UI calls it on every Tokens-tab open.
        const auto st = parseObj(p.faucetStatus());
        QCOMPARE(st[QStringLiteral("available")].toBool(), true);
        const QString cliArgv = qEnvironmentVariable("MEDUSA_WALLET_CLI") + QStringLiteral(".argv");
        QVERIFY2(!slurp(cliArgv).contains(QStringLiteral("new")),
                 "faucetStatus created an account - a read-only status call must not write");

        const auto r = parseObj(p.startTokenFaucet(QStringLiteral("Public/a"), kPw));
        QVERIFY2(r.contains(QStringLiteral("jobId")), qPrintable(QJsonDocument(r).toJson()));
        awaitJob(p, r[QStringLiteral("jobId")].toString());

        // One account per definition, each recorded as that definition's vault BEFORE the
        // claim: the cooldown marker PDA is derived from the first recipient, so a claim that
        // minted fresh accounts every time would never bind to a marker at all.
        const QString argv = slurp(cliArgv);
        QCOMPARE(argv.count(QStringLiteral("\nnew\n")), kParadoxDefs.size());
        for (const QString& def : kParadoxDefs)
            QVERIFY2(argv.contains(QStringLiteral("\nvault\n") + def),
                     qPrintable(QStringLiteral("no vault was recorded for ") + def));

        const QStringList cargv = slurp(qEnvironmentVariable("MEDUSA_FAUCET_CLIENT")
                                        + QStringLiteral(".argv"))
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const int acct = cargv.indexOf(QStringLiteral("--account"));
        QVERIFY(acct >= 0);
        QCOMPARE(cargv.at(acct + 1), QString("minted1,minted2,minted3"));
    }

    // Vaults belong to ONE account. A holding recorded for account A must never be handed to a
    // claim made from account B: the registry used to key vaults by definition alone, so the
    // faucet always paid the same holding whichever account you claimed from, only that account
    // could shield, and every other account displayed tokens it could not spend. B has no vault
    // of its own here, so it must MINT fresh holdings rather than inherit A's.
    void testAVaultBelongsToOneAccountAndIsNeverReusedByAnother()
    {
        useWorkingFaucet();
        useCli(makeTokenCli(registryWithVaults(kParadoxDefs, QStringLiteral("account-A"))));
        WalletPlugin p;
        arm(p);

        const auto r = parseObj(p.startTokenFaucet(QStringLiteral("Public/account-B"), kPw));
        QVERIFY2(r.contains(QStringLiteral("jobId")), qPrintable(QJsonDocument(r).toJson()));
        awaitJob(p, r[QStringLiteral("jobId")].toString());

        const QStringList argv = slurp(qEnvironmentVariable("MEDUSA_FAUCET_CLIENT")
                                       + QStringLiteral(".argv"))
                                     .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const int acct = argv.indexOf(QStringLiteral("--account"));
        QVERIFY(acct >= 0);
        const QString recipients = argv.at(acct + 1);
        // Not A's vaults, and not B's own account either (an account holds one definition).
        for (const QString& v : vaultIds(kParadoxDefs.size()))
            QVERIFY2(!recipients.contains(v),
                     qPrintable(QStringLiteral("B was handed A's vault %1: %2").arg(v, recipients)));
        QVERIFY2(!recipients.contains(QStringLiteral("account-B")), qPrintable(recipients));
    }

    // A claim never uses the user's own account as a recipient. On rc5 an account holds exactly
    // ONE token definition, so spending the main account here would bind it to one token
    // permanently - the wallet's per-definition holdings are the only correct targets.
    void testAClaimTargetsThePerTokenHoldingsAndNeverTheUsersAccount()
    {
        useWorkingFaucet();
        WalletPlugin p;
        arm(p);

        const auto r = parseObj(p.startTokenFaucet(QStringLiteral("Public/my-main-account"), kPw));
        QVERIFY2(r.contains(QStringLiteral("jobId")), qPrintable(QJsonDocument(r).toJson()));
        awaitJob(p, r[QStringLiteral("jobId")].toString());

        const QString client = qEnvironmentVariable("MEDUSA_FAUCET_CLIENT");
        const QStringList argv = slurp(client + QStringLiteral(".argv"))
                                     .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY2(argv.contains(QStringLiteral("claim")), qPrintable(argv.join(QLatin1Char(' '))));
        const int acct = argv.indexOf(QStringLiteral("--account"));
        const int defs = argv.indexOf(QStringLiteral("--definitions"));
        QVERIFY(acct >= 0 && defs >= 0);
        QCOMPARE(argv.at(acct + 1), vaultIds(kParadoxDefs.size()).join(QLatin1Char(',')));
        // …and the definitions are the ACTIVE ZONE's, from the module's table.
        QCOMPARE(argv.at(defs + 1), kParadoxDefs.join(QLatin1Char(',')));
        QVERIFY2(!argv.at(acct + 1).contains(QStringLiteral("my-main-account")),
                 "the claim used the user's own account as a token holding");
        // One recipient per definition, in the same order: the client pairs them positionally.
        QCOMPARE(argv.at(acct + 1).split(QLatin1Char(',')).size(),
                 argv.at(defs + 1).split(QLatin1Char(',')).size());
    }

    // The client is not the wallet wrapper, so nothing defaults LEE_WALLET_HOME_DIR for it. If
    // the module does not pass its own home the client silently operates on a DIFFERENT wallet:
    // wrong keys, wrong sequencer, and a claim that looks like it did nothing.
    void testTheFaucetClientIsGivenTheSameWalletHomeTheModuleUses()
    {
        useWorkingFaucet();
        WalletPlugin p;
        arm(p);
        const auto r = parseObj(p.startTokenFaucet(QStringLiteral("Public/a"), kPw));
        QVERIFY(r.contains(QStringLiteral("jobId")));
        awaitJob(p, r[QStringLiteral("jobId")].toString());

        const QString home = slurp(qEnvironmentVariable("MEDUSA_FAUCET_CLIENT")
                                   + QStringLiteral(".home")).trimmed();
        QCOMPARE(home, qEnvironmentVariable("LEE_WALLET_HOME_DIR"));
    }

    // The one failure the wallet cannot pre-check: initialized treasuries with nothing in them.
    // The chain answers by silently dropping the transaction, and "not included within 4 blocks"
    // is not something a user can act on. It must be translated, and the raw text kept.
    void testAnEmptyTreasuryIsReportedAsUnfundedNotAsASilentRejection()
    {
        qputenv("MEDUSA_FAUCET_CLIENT", makeFaucetClient(
            kFaucetId,
            QStringLiteral("{\"error\":\"transaction 0xabc was not included within 4 blocks - the "
                           "sequencer rejected it silently (program execution failed on-chain)\"}"),
            1).toUtf8());
        qputenv("MEDUSA_FAUCET_BIN", makeFaucetGuestBin().toUtf8());
        useCli(makeTokenCli(registryWithVaults(kParadoxDefs)));

        WalletPlugin p;
        arm(p);
        const auto r = parseObj(p.startTokenFaucet(QStringLiteral("Public/a"), kPw));
        QVERIFY(r.contains(QStringLiteral("jobId")));
        const auto job = awaitJob(p, r[QStringLiteral("jobId")].toString());

        QCOMPARE(job[QStringLiteral("state")].toString(), QString("error"));
        QCOMPARE(job[QStringLiteral("reason")].toString(), QString("not-funded"));
        const QString msg = job[QStringLiteral("error")].toString();
        QVERIFY2(!msg.contains(QStringLiteral("not included within")),
                 qPrintable(QStringLiteral("the raw rejection was shown to the user: ") + msg));
        // "Fund it" is only an instruction if it says WHICH accounts. They come from this
        // zone's row in the table, and they are the only place those addresses exist.
        for (const QString& t : kParadoxTreasuries)
            QVERIFY2(msg.contains(t), qPrintable(QStringLiteral("treasury not named: ") + msg));
        // Kept, not discarded: an operator debugging the zone still needs the real text.
        QVERIFY2(job[QStringLiteral("rawError")].toString().contains(QStringLiteral("not included")),
                 "the underlying sequencer message was thrown away");
    }

    // ══ PARTIAL SUCCESS IS THE NORMAL CASE ════════════════════════════════════════════════
    // pinata's cooldown and the token program's 6h marker cooldown are independent, so the two
    // halves drift apart the moment anyone claims twice. "150 LEZ arrived, tokens are on
    // cooldown" has to come back as two terminal jobs whose outcomes are separately readable -
    // one machine code each - or a caller has to guess from prose which half did what.
    void testALezClaimSucceedsWhileTheTokenHalfIsOnCooldown()
    {
        qputenv("MEDUSA_FAUCET_CLIENT", makeFaucetClient(
            kFaucetId,
            QStringLiteral("{\"error\":\"cooldown not elapsed: 214 minutes remaining before the "
                           "next claim\"}"),
            1).toUtf8());
        qputenv("MEDUSA_FAUCET_BIN", makeFaucetGuestBin().toUtf8());
        useCli(makeTokenCli(registryWithVaults(kParadoxDefs)));

        WalletPlugin p;
        arm(p);
        // Half 1: native LEZ, ungated, succeeds.
        const auto lez = parseObj(p.startFaucet(QStringLiteral("abc123")));
        QVERIFY2(lez.contains(QStringLiteral("jobId")), qPrintable(QJsonDocument(lez).toJson()));
        // Half 2: the tokens, gated, refused on-chain by the cooldown.
        const auto tok = parseObj(p.startTokenFaucet(QStringLiteral("abc123"), kPw));
        QVERIFY2(tok.contains(QStringLiteral("jobId")), qPrintable(QJsonDocument(tok).toJson()));

        const auto lj = awaitJob(p, lez[QStringLiteral("jobId")].toString());
        const auto tj = awaitJob(p, tok[QStringLiteral("jobId")].toString());

        // The LEZ half is NOT dragged down with the token half.
        QCOMPARE(lj[QStringLiteral("state")].toString(), QString("done"));
        QCOMPARE(lj[QStringLiteral("op")].toString(),    QString("faucet"));
        // …and the token half says WHY, as a code and not only as a sentence.
        QCOMPARE(tj[QStringLiteral("state")].toString(),  QString("error"));
        QCOMPARE(tj[QStringLiteral("op")].toString(),     QString("tokenfaucet"));
        QCOMPARE(tj[QStringLiteral("reason")].toString(), QString("cooldown"));
        // The client's own wording survives untouched: it carries the figure a countdown is
        // rendered from, and inventing a replacement sentence would throw the number away.
        QVERIFY2(tj[QStringLiteral("error")].toString().contains(QStringLiteral("214 minutes")),
                 qPrintable(tj[QStringLiteral("error")].toString()));
    }

    // The mirror image, which is just as normal: LEZ is still on its own cooldown while the
    // tokens are claimable. A refusal on one half must never suppress or cancel the other.
    void testTheTokenHalfStillRunsWhenTheLezHalfIsRefused()
    {
        qputenv("MEDUSA_FAUCET_CLIENT", makeFaucetClient(kFaucetId).toUtf8());
        qputenv("MEDUSA_FAUCET_BIN",    makeFaucetGuestBin().toUtf8());
        // A wallet CLI whose `pinata claim` takes a moment and then FAILS, and which answers
        // the registry reads normally. The delay is what makes this deterministic: the token
        // claim really is queued behind a running native claim, so the release path being
        // tested is "the job ahead ended in error", not "there was nothing to wait for".
        const QString path = m_tmp.path() + QStringLiteral("/cooldown_wallet.sh");
        useCli(writeExec(path, QStringLiteral(
            "#!/bin/sh\n"
            "if [ \"$1\" = pinata ]; then\n"
            "  sleep 1\n"
            "  echo 'faucet cooldown not elapsed: 42 minutes remaining' >&2; exit 1\n"
            "fi\n"
            "if [ \"$1\" = token-registry ] && [ \"$2\" != vault ]; then echo '%1'; exit 0; fi\n"
            "echo '{\"ok\":true}'\n").arg(registryWithVaults(kParadoxDefs, QStringLiteral("abc123")))));

        WalletPlugin p;
        arm(p);
        const auto lez = parseObj(p.startFaucet(QStringLiteral("abc123")));
        const auto tok = parseObj(p.startTokenFaucet(QStringLiteral("abc123"), kPw));
        QVERIFY(lez.contains(QStringLiteral("jobId")) && tok.contains(QStringLiteral("jobId")));
        QCOMPARE(parseObj(p.getJob(tok[QStringLiteral("jobId")].toString()))
                     [QStringLiteral("phase")].toString(), QString("queued"));

        const auto lj = awaitJob(p, lez[QStringLiteral("jobId")].toString(), 8000);
        const auto tj = awaitJob(p, tok[QStringLiteral("jobId")].toString(), 8000);

        QCOMPARE(lj[QStringLiteral("state")].toString(),  QString("error"));
        QCOMPARE(lj[QStringLiteral("reason")].toString(), QString("cooldown"));
        QCOMPARE(tj[QStringLiteral("state")].toString(),  QString("done"));
        QVERIFY2(slurp(qEnvironmentVariable("MEDUSA_FAUCET_CLIENT") + QStringLiteral(".argv"))
                     .contains(QStringLiteral("claim")),
                 "a refused LEZ claim suppressed the token claim");
    }

    // ORDER. The two halves run as separate jobs, and they must not drive the wallet store at
    // the same time: `pinata claim` may run `auth-transfer init` for the recipient first (the
    // pinata program credits without claiming, so the chain rejects a credit to an account it
    // has no record of), and both halves are separate processes on one storage.json. A token
    // claim started while a native claim is in flight is therefore QUEUED behind it.
    void testTheTokenClaimIsQueuedBehindAnInFlightNativeClaim()
    {
        qputenv("MEDUSA_FAUCET_CLIENT", makeFaucetClient(kFaucetId).toUtf8());
        qputenv("MEDUSA_FAUCET_BIN",    makeFaucetGuestBin().toUtf8());
        // A CLI whose `pinata claim` takes a moment, so the token claim really does arrive
        // while it is running; everything else answers at once.
        const QString path = m_tmp.path() + QStringLiteral("/slow_wallet.sh");
        useCli(writeExec(path, QStringLiteral(
            "#!/bin/sh\n"
            "if [ \"$1\" = pinata ]; then sleep 1; echo '{\"ok\":true,\"txHash\":\"tx1\"}'; exit 0; fi\n"
            "if [ \"$1\" = token-registry ] && [ \"$2\" != vault ]; then echo '%1'; exit 0; fi\n"
            "echo '{\"ok\":true}'\n").arg(registryWithVaults(kParadoxDefs, QStringLiteral("abc123")))));

        WalletPlugin p;
        arm(p);
        const QString lezJob = parseObj(p.startFaucet(QStringLiteral("abc123")))
                                   [QStringLiteral("jobId")].toString();
        const QString tokJob = parseObj(p.startTokenFaucet(QStringLiteral("abc123"), kPw))
                                   [QStringLiteral("jobId")].toString();
        QVERIFY(!lezJob.isEmpty() && !tokJob.isEmpty());

        // The token job is live and pollable, but its child has not been launched: the only
        // thing the client has run is the preflight's offline `info` probe, never the claim.
        const auto queued = parseObj(p.getJob(tokJob));
        QCOMPARE(queued[QStringLiteral("state")].toString(), QString("running"));
        QCOMPARE(queued[QStringLiteral("phase")].toString(), QString("queued"));
        const QString clientArgv = qEnvironmentVariable("MEDUSA_FAUCET_CLIENT")
                                 + QStringLiteral(".argv");
        QVERIFY2(!slurp(clientArgv).contains(QStringLiteral("claim")),
                 "the token claim ran beside the native claim instead of behind it");

        // Once the native claim ends, the queued one starts and runs to completion by itself.
        awaitJob(p, lezJob, 8000);
        const auto tj = awaitJob(p, tokJob, 8000);
        QCOMPARE(tj[QStringLiteral("state")].toString(), QString("done"));
        QVERIFY2(slurp(qEnvironmentVariable("MEDUSA_FAUCET_CLIENT") + QStringLiteral(".argv"))
                     .contains(QStringLiteral("claim")),
                 "the queued token claim never started");
    }


    // The guest .bin is data, but it still decides WHICH PROGRAM a signed claim addresses, so it
    // follows the same rule as every binary this module resolves: on a packaged install the
    // bundle is the only place looked at, and a copy planted in the uid-writable ~/.local is
    // never selected. (This test binary has a bundle dir; initTestCase creates it.)
    void testAPlantedGuestBinaryOutsideTheBundleIsNeverSelected()
    {
        const QString bundleDir = QCoreApplication::applicationDirPath() + QStringLiteral("/bin");
        QVERIFY(QDir(bundleDir).exists());
        QVERIFY(!QFile::exists(bundleDir + QStringLiteral("/medusa_faucet.bin")));

        // A fake home so the plant never touches the developer's real one.
        const QByteArray prevHome = qgetenv("HOME");
        const QString fakeHome = m_tmp.path() + QStringLiteral("/planted-home");
        QDir().mkpath(fakeHome + QStringLiteral("/.local/bin"));
        QDir().mkpath(fakeHome + QStringLiteral("/.local/share/medusa"));
        for (const QString& p : { fakeHome + QStringLiteral("/.local/bin/medusa_faucet.bin"),
                                  fakeHome + QStringLiteral("/.local/share/medusa/medusa_faucet.bin") }) {
            QFile f(p);
            f.open(QIODevice::WriteOnly);
            f.write("planted");
            f.close();
        }
        qputenv("HOME", fakeHome.toUtf8());
        // A client IS present, so the preflight gets past step 1 and really does resolve the .bin.
        qputenv("MEDUSA_FAUCET_CLIENT", makeFaucetClient(kFaucetId).toUtf8());

        WalletPlugin p;
        const auto r = parseObj(p.faucetStatus());

        if (prevHome.isEmpty()) qunsetenv("HOME"); else qputenv("HOME", prevHome);

        QCOMPARE(r[QStringLiteral("binFound")].toBool(), false);
        QCOMPARE(r[QStringLiteral("reason")].toString(), QString("bin-missing"));
        QVERIFY2(!r[QStringLiteral("bin")].toString().contains(QStringLiteral("/.local/")),
                 qPrintable(QStringLiteral("a planted guest binary was selected: ")
                            + r[QStringLiteral("bin")].toString()));
    }

    // THE CONSERVATIVE PATH, PINNED. The client-side faucet is the only one that has ever
    // delivered a token on these zones, so adding the on-chain one must not have disturbed it:
    // startFaucet is still ungated, still `pinata claim`, and still works when the on-chain
    // faucet is entirely absent.
    void testTheClientSideFaucetIsUnchangedAndStillWorksWithNoOnChainFaucet()
    {
        useCli(makeRecordingCli(QStringLiteral("{\"ok\":true,\"txHash\":\"tx1\"}")));
        WalletPlugin p;                       // never unlocked: startFaucet takes no password
        const auto r = parseObj(p.startFaucet(QStringLiteral("abc123")));
        QVERIFY2(r.contains(QStringLiteral("jobId")), qPrintable(QJsonDocument(r).toJson()));
        awaitJob(p, r[QStringLiteral("jobId")].toString());

        const QStringList argv = slurp(qEnvironmentVariable("MEDUSA_WALLET_CLI")
                                       + QStringLiteral(".argv"))
                                     .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(argv.value(0), QString("pinata"));
        QCOMPARE(argv.value(1), QString("claim"));
        QCOMPARE(argv.value(2), QString("--to"));
        QCOMPARE(argv.value(3), QString("Public/abc123"));
        // And the on-chain faucet was not consulted, let alone substituted for it.
        QVERIFY2(!argv.contains(QStringLiteral("--definitions")),
                 "startFaucet was rerouted through the on-chain faucet");
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
        auto r = parseObj(p.startShield(QStringLiteral("Public/a"), QStringLiteral("Private/b"),
                                        QStringLiteral(""), kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testShieldRejectsPrivateSource()
    {
        // Shield source must be Public - a Private/ source is a prefix conflict.
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.startShield(QStringLiteral("Private/a"), QStringLiteral("Private/b"),
                                        QStringLiteral("10"), kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testDeshieldRejectsPublicSource()
    {
        WalletPlugin p;
        arm(p);
        auto r = parseObj(p.startDeshield(QStringLiteral("Public/a"), QStringLiteral("Public/b"),
                                          QStringLiteral("10"), kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
    }

    void testForeignTransferRequiresKeys()
    {
        WalletPlugin p;
        arm(p);
        // npk missing from the recipient spec: named, and refused before any proof starts.
        auto r = parseObj(p.startPrivateTransfer(
            QStringLiteral("Private/a"),
            QStringLiteral(R"({"vpk":"vpk","identifier":"id"})"),
            QStringLiteral("10"), kPw));
        QCOMPARE(r[QStringLiteral("error")].toString(), QString("recipient npk is required"));
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
        auto started = parseObj(p.startShield(QStringLiteral("Public/a"),
                                              QStringLiteral("Private/b"),
                                              QStringLiteral("10"), kPw));
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
        auto started = parseObj(p.startPrivateTransfer(
            QStringLiteral("Private/a"), QStringLiteral("Private/b"),
            QStringLiteral(R"({"asset":"token","definitionId":"def1","amount":"5"})"), kPw));
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
        auto started = parseObj(p.startDeshield(QStringLiteral("Private/a"),
                                                QStringLiteral("Public/b"),
                                                QStringLiteral("10"), kPw));
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
        // Carried on EVERY reply: after a cancel the wallet may have landed on a clearnet zone,
        // and a caller still has to tell "the user stopped it" from "it went quiet".
        QVERIFY(r.contains(QStringLiteral("connectAborted")));
        QCOMPARE(r[QStringLiteral("connectAborted")].toBool(), false);
    }

    // A Tor zone must get the same machine-readable `reason` a local zone does. It did not: the
    // header documented "aborted | tor-missing | tor-failed | tor-exited | mismatch" and the body
    // emitted none of them, so a Tor that crashed or never started reported state "starting" with
    // nothing to explain it, forever. That is the eternal silent "Connecting..." this record
    // exists to end, and the backing members were dead too (m_torExited was cleared but never
    // set, m_torStopping was never referenced), so the codes were unreachable by construction.
    void testATorZoneReportsAReasonAndAnAbortRecord()
    {
        WalletPlugin p;
        const QString zone = useUserZone(p, QStringLiteral("Hidden node"));
        // Make it a Tor zone: editZone with an onion is the same path addZone takes.
        p.editZone(zone, QStringLiteral("Hidden node"),
                   QStringLiteral("abcdefghijklmnop.onion:3077"), true);

        const auto r = parseObj(p.getSequencerStatus());
        QVERIFY2(r.contains(QStringLiteral("reason")), qPrintable(QJsonDocument(r).toJson()));
        QVERIFY(r.contains(QStringLiteral("torRunning")));
        QVERIFY(r.contains(QStringLiteral("forwardRunning")));
        // Whether a system tor exists is a property of the BOX, not of the wallet, so assert the
        // contract rather than one environment's answer: the reason must be one of the documented
        // codes. Pinning "tor-missing" here passed or failed depending on whether /usr/bin/tor
        // happened to be installed.
        const QStringList kTorReasons{ QString(), QStringLiteral("aborted"),
                                       QStringLiteral("tor-missing"), QStringLiteral("tor-failed"),
                                       QStringLiteral("tor-exited"), QStringLiteral("mismatch") };
        QVERIFY2(kTorReasons.contains(r[QStringLiteral("reason")].toString()),
                 qPrintable(QStringLiteral("undocumented Tor reason: ")
                            + r[QStringLiteral("reason")].toString()));

        // Cancelling records WHICH zone was abandoned, and the next status call still says so.
        const auto c = parseObj(p.cancelConnect());
        QCOMPARE(c[QStringLiteral("ok")].toBool(), true);
        QCOMPARE(c[QStringLiteral("abortedZone")].toString(), zone);
        const auto after = parseObj(p.getSequencerStatus());
        QCOMPARE(after[QStringLiteral("connectAborted")].toBool(), true);
        QCOMPARE(after[QStringLiteral("abortedZone")].toString(), zone);
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
            QStringLiteral("client"),     // = resolveBin("medusa-faucet-client", MEDUSA_FAUCET_CLIENT)
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
        QVERIFY(src.contains(QStringLiteral("const QString client = resolveBin(QStringLiteral(\"medusa-faucet-client\")")));
        // …and the on-chain faucet's guest .bin is NOT on this list on purpose: it is risc0
        // bytecode, read as DATA and handed to the client as an argument. It must never appear
        // as a startChild() program, however tempting the name `bin` makes it.
        QVERIFY2(!src.contains(QRegularExpression(
                     QStringLiteral("startChild\\([^,]+,\\s*pf\\.bin\\b"))),
                 "the faucet guest .bin reached startChild() as a program");
        // The `bin` local gained a second source when jobs learned to run their own binary.
        // Pin the whole expression, so it cannot quietly widen to an arbitrary path.
        QVERIFY(src.contains(QStringLiteral(
            "const QString bin   = binOverride.isEmpty() ? cliPath() : binOverride;")));
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

    // ONE named constant, spelled out ONCE. A program id is derived from the guest ELF alone, so
    // a second copy of this literal - or a per-zone table of them - would be a second place that
    // can drift from the deployed program, and drift here means claims aimed at a program that
    // is not there. Also pins that the id is never assembled at runtime from the active zone.
    void testTheFaucetProgramIdIsOneLiteralAndNotAPerZoneTable()
    {
        const QString src = pluginCode();
        QCOMPARE(src.count(kFaucetId), 1);
        QCOMPARE(src.count(QStringLiteral("kFaucetProgramId =")), 1);   // one definition
        // Every other 64-hex literal in the file would be a rival program id. The one that is
        // allowed is the standalone sequencer's bedrock channel_id inside kSeqConfigTemplate,
        // named here so a THIRD one cannot hide behind a loosened count.
        static const QRegularExpression hex64(QStringLiteral("\"[0-9a-f]{64}\""));
        QCOMPARE(src.count(hex64), 2);
        QCOMPARE(src.count(QStringLiteral(
            "\"0202020202020202020202020202020202020202020202020202020202020202\"")), 1);
        // The constant is used, and used to VERIFY rather than merely to display: the preflight
        // compares the local guest binary's recomputed id against it.
        QVERIFY(src.contains(QStringLiteral("pf.verified = (got == QString::fromLatin1(kFaucetProgramId));")));
    }

    // ══ ONE TABLE, NOT A SCATTER ══════════════════════════════════════════════════════════
    // The program id is one constant for every zone; the TOKENS are per-zone, and they live in
    // exactly one table. A second copy of a definition or a treasury id anywhere in the module
    // is the beginning of the drift this table exists to prevent: the whole point is that
    // adding a zone to the faucet is adding a row, in one place.
    void testEveryFaucetTokenAndTreasuryAppearsExactlyOnceInOneTable()
    {
        const QString src = pluginCode();
        for (const QString& id : kParadoxDefs + kLogosDefs + kParadoxTreasuries)
            QCOMPARE(src.count(id), 1);
        // Both zones are in the table, and nothing else is.
        QCOMPARE(src.count(QStringLiteral("kFaucetZones[] =")), 1);
        static const QRegularExpression rows(QStringLiteral("\\{\\s*\"[a-z-]+\", \\{"));
        QCOMPARE(src.count(rows), 2);
        // The capability is decided by the table alone. faucetZoneRow() is the only reader, and
        // no code path answers "does this zone have a token faucet" from a stored record.
        QCOMPARE(src.count(QStringLiteral("const FaucetZoneRow* faucetZoneRow(")), 1);
        QVERIFY2(!src.contains(QRegularExpression(
                     QStringLiteral("value\\(QStringLiteral\\(\"tokenFaucet\"\\)\\)"))),
                 "the token-faucet capability is read back out of a zone record somewhere");
    }

    // ══ INVARIANT B, THROUGH THE QUEUED START ═════════════════════════════════════════════
    // A queued job is launched later, from a path stored on the job, so the enumeration in
    // testNoProcessIsEverLaunchedByABareName (which sees only `startChild(*proc, bin, args)`)
    // is now one hop away from the resolver. Pin that hop: `pendingBin` is written in exactly
    // one place, from the same expression the immediate start uses, and it is only ever read
    // back into startJobProcess.
    void testAQueuedJobsProgramComesFromTheSameResolverAsAnImmediateOne()
    {
        const QString src = pluginCode();
        QCOMPARE(src.count(QStringLiteral("j->pendingBin  = bin;")), 1);
        QVERIFY(src.contains(QStringLiteral(
            "const QString bin   = binOverride.isEmpty() ? cliPath() : binOverride;")));
        // Every startJobProcess() call site, and where its program comes from.
        static const QRegularExpression call(
            QStringLiteral("startJobProcess\\(\\s*[A-Za-z_>&.-]+,\\s*([A-Za-z_][A-Za-z0-9_>.-]*)"));
        QStringList seen, bad;
        QRegularExpressionMatchIterator it = call.globalMatch(src);
        while (it.hasNext()) {
            const QString prog = it.next().captured(1);
            seen << prog;
            if (prog != QStringLiteral("bin")                     // the resolver's own output
                && prog != QStringLiteral("job->pendingBin")      // …stored, in the cap timer
                && prog != QStringLiteral("q->pendingBin"))       // …stored, on release
                bad << prog;
        }
        QVERIFY2(bad.isEmpty(), qPrintable(QStringLiteral("startJobProcess with an unrecognised "
                                                          "program source: ")
                                           + bad.join(QStringLiteral(", "))));
        QVERIFY2(seen.size() == 3, qPrintable(QStringLiteral("expected 3 launch sites, found %1")
                                                  .arg(seen.size())));
    }

    // ══ THE CLIENT-SIDE TOKEN FAUCET IS GONE FROM THE WRAPPER ═════════════════════════════
    // It sent tokens from a treasury wallet whose whitelist lived in a directory outside the
    // install, so it silently stopped working the moment that directory went away - while the
    // LEZ half kept succeeding, which is exactly how the bug presented. Deleting it is the
    // point; leaving a dormant copy behind would let it come back.
    void testTheWrapperNoLongerDistributesTokensItself()
    {
        const QString wrapper = slurp(QStringLiteral(QT_TESTCASE_SOURCEDIR)
                                      + QStringLiteral("/scripts/wallet-wrapper"));
        QVERIFY2(!wrapper.isEmpty(), "could not read wallet-wrapper");
        QStringList code;
        for (const QString& l : wrapper.split(QLatin1Char('\n')))
            if (!l.trimmed().startsWith(QLatin1Char('#'))) code << l;
        const QString src = code.join(QLatin1Char('\n'));

        // The treasury wallet, its whitelist file, and the per-claim distribution.
        for (const QString& gone : { QStringLiteral("TREASURY_HOME"),
                                     QStringLiteral("faucet_tokens("),
                                     QStringLiteral("run_treasury("),
                                     QStringLiteral("load_whitelist("),
                                     QStringLiteral("seed_whitelist("),
                                     QStringLiteral("medusa-treasury") })
            QVERIFY2(!src.contains(gone),
                     qPrintable(QStringLiteral("the client-side token faucet is still here: ")
                                + gone));
        // The LEZ half is untouched, including the registration that is the REASON the token
        // half has to run after it.
        QVERIFY(src.contains(QStringLiteral("\"pinata\" and args[1] == \"claim\"")));
        QVERIFY(src.contains(QStringLiteral("auth-transfer\", \"init\"")));
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
        // The wallet CLI resolves the same way with no env override. The bundled wrapper is
        // "medusa-wallet" (namespaced like every other binary we ship); the bare "wallet" name
        // survives only as a one-release fallback for installs made before the rename.
        qunsetenv("MEDUSA_WALLET_CLI");
        WalletPlugin p2;
        const QString cli = parseObj(p2.getConfig())[QStringLiteral("cliPathEff")].toString();
        QCOMPARE(cli, bundleDir + QStringLiteral("/medusa-wallet"));
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

    // ══ THE BRIDGE'S ARGUMENT CEILING ═══════════════════════════════════════════════════════
    //
    // These two tests exist because the class of bug they pin SHIPPED, twice, and neither time
    // did anything fail. The Logos bridge dispatches a remote call through a switch over the
    // argument count - qt_provider_object.cpp:20-34:
    //
    //     switch (args.size()) { case 0: … case 5: return QMetaObject::invokeMethod(…); }
    //     default: qWarning() << "QtProviderObject: Currently supports 0-5 arguments. Got:"
    //                         << args.size();
    //
    // so a 6-argument verb is never invoked at all, and LogosQmlBridge.cpp:76 hands the caller
    // {"error":"Invalid response"} - which looks exactly like a wallet error. startShield and
    // startDeshield went over when the fifth security round appended the session password;
    // startPrivateTransferForeign was over from the day it was written and nobody noticed for
    // months, because nothing ever exercised it through the real bridge. Both facts were found
    // by the owner, live, from a Basecamp log - which is the whole reason this is a test.
    //
    // Read off the metaobject, which is the SAME table the bridge dispatches against, so no
    // future verb can be added below the radar of this check.

    // A verb over the ceiling can never be called. Nothing may exceed 5, ever.
    void testEveryInvokableFitsTheBridgeCeiling()
    {
        const QMetaObject& mo = WalletPlugin::staticMetaObject;
        QStringList over;
        for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
            const QMetaMethod m = mo.method(i);
            if (m.methodType() == QMetaMethod::Signal)
                continue;
            if (m.parameterCount() > 5)
                over << QStringLiteral("%1 takes %2")
                            .arg(QString::fromUtf8(m.name())).arg(m.parameterCount());
        }
        QVERIFY2(over.isEmpty(),
                 qPrintable(QStringLiteral("the Logos bridge marshals 0-5 arguments and drops "
                                           "anything wider (qt_provider_object.cpp:20-34), so "
                                           "these verbs can never be invoked: ")
                            + over.join(QStringLiteral("; "))));
    }

    // …and the house rule is 4, not 5. A gated verb sitting exactly on the ceiling is one
    // security round away from death: that is precisely what happened to shield and deshield
    // when the password was appended. Anything at 5 has no room for the next argument.
    void testEveryInvokableKeepsBridgeHeadroom()
    {
        const QMetaObject& mo = WalletPlugin::staticMetaObject;
        QStringList atCeiling;
        for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
            const QMetaMethod m = mo.method(i);
            if (m.methodType() == QMetaMethod::Signal)
                continue;
            if (m.parameterCount() == 5)
                atCeiling << QString::fromUtf8(m.name());
        }
        QVERIFY2(atCeiling.isEmpty(),
                 qPrintable(QStringLiteral("these verbs sit exactly on the bridge's 5-argument "
                                           "ceiling, with no room for the next one; collapse two "
                                           "arguments that are really one concept (see the value "
                                           "and recipient specs): ")
                            + atCeiling.join(QStringLiteral(", "))));
    }

    // The other half of the same contract: medusa_ui's callGated() appends the session password
    // POSITIONALLY as the last argument, so a gated verb whose password is not last would be
    // handed some other value to prove - and would refuse every spend the user makes.
    void testEveryGatedVerbKeepsThePasswordLast()
    {
        // Mirrors Main.qml's gatedVerbs list, which mirrors the authorize() sites.
        static const QStringList kGated{
            QStringLiteral("sendTransfer"),      QStringLiteral("startSendTransfer"),
            QStringLiteral("startSendToken"),    QStringLiteral("startShield"),
            QStringLiteral("startDeshield"),     QStringLiteral("startPrivateTransfer"),
            QStringLiteral("consolidateToken"),  QStringLiteral("approveAction"),
            QStringLiteral("approveZone"),       QStringLiteral("exportMnemonic"),
            QStringLiteral("exportKey"),         QStringLiteral("startTokenFaucet"),
            QStringLiteral("resetWallet"),       QStringLiteral("restoreWallet"),
            QStringLiteral("setCliPath") };
        // A defaulted parameter makes moc emit ONE metamethod PER ARITY (resetWallet() and
        // resetWallet(QString) are two entries), so compare the FULL form of each verb - the
        // one callGated() targets when it appends the password.
        const QMetaObject& mo = WalletPlugin::staticMetaObject;
        QHash<QString, QMetaMethod> fullest;
        for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
            const QMetaMethod m = mo.method(i);
            const QString name = QString::fromUtf8(m.name());
            if (m.methodType() == QMetaMethod::Signal || !kGated.contains(name))
                continue;
            if (!fullest.contains(name)
                || fullest.value(name).parameterCount() < m.parameterCount())
                fullest.insert(name, m);
        }
        QStringList seen, wrong;
        for (auto it = fullest.constBegin(); it != fullest.constEnd(); ++it) {
            const QString name = it.key();
            const QMetaMethod m = it.value();
            QVERIFY2(m.parameterCount() > 0, qPrintable(name));
            seen << name;
            const QString last = QString::fromUtf8(m.parameterNames().last());
            // restoreWallet's credential is the CURRENT session password and is named for it;
            // everything else calls it `password`.
            if (last != QStringLiteral("password") && last != QStringLiteral("sessionPassword"))
                wrong << QStringLiteral("%1's last argument is `%2`").arg(name, last);
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral("; "))));
        // And every gated verb was actually found - a renamed verb must not silently drop off.
        for (const QString& g : kGated)
            QVERIFY2(seen.contains(g), qPrintable(QStringLiteral("gated verb missing from the "
                                                                 "metaobject: ") + g));
    }

    // ── The value spec: one argument for "how much of what" ──────────────────────────────
    void testNativeValueSpecIsABareAmount()
    {
        const QString cli = makeRecordingCli(R"({"ok":true,"txId":"t1"})");
        useCli(cli);
        WalletPlugin p;
        arm(p);
        const auto started = parseObj(p.startShield(QStringLiteral("Public/a"),
                                                    QStringLiteral("Private/b"),
                                                    QStringLiteral("10"), kPw));
        const QString jobId = started[QStringLiteral("jobId")].toString();
        QVERIFY(!jobId.isEmpty());
        QCOMPARE(awaitJob(p, jobId)[QStringLiteral("state")].toString(), QString("done"));
        QCOMPARE(slurp(cli + QStringLiteral(".argv")),
                 QString("auth-transfer\nsend\n--from\nPublic/a\n--to\nPrivate/b\n"
                         "--amount\n10\n"));
    }

    void testTokenValueSpecCarriesItsDefinitionIntoTheJob()
    {
        const QString cli = makeRecordingCli(R"({"ok":true,"txId":"t1"})");
        useCli(cli);
        WalletPlugin p;
        arm(p);
        const auto started = parseObj(p.startShield(
            QStringLiteral("Public/a"), QStringLiteral("Private/b"),
            QStringLiteral(R"({"asset":"token","definitionId":"DEF9","amount":"7"})"), kPw));
        const QString jobId = started[QStringLiteral("jobId")].toString();
        QVERIFY(!jobId.isEmpty());
        QCOMPARE(awaitJob(p, jobId)[QStringLiteral("state")].toString(), QString("done"));
        // The definition reaches the wrapper's token-shield verb, which is the whole reason
        // definitionId existed as a separate argument - it is not lost by the collapse.
        QCOMPARE(slurp(cli + QStringLiteral(".argv")),
                 QString("token-shield\nPublic/a\nPrivate/b\nDEF9\n7\n"));
        QCOMPARE(parseObj(p.getJob(jobId))[QStringLiteral("asset")].toString(), QString("token"));
    }

    // A JSON number is a legitimate way to write an amount, and it must arrive unrounded: a
    // fixed-precision conversion would turn 12.5 into 13 and move value nobody asked to move.
    void testValueSpecTakesAJsonNumberWithoutRounding()
    {
        const QString cli = makeRecordingCli(R"({"ok":true,"txId":"t1"})");
        useCli(cli);
        WalletPlugin p;
        arm(p);
        const auto whole = parseObj(p.startShield(QStringLiteral("Public/a"),
                                                  QStringLiteral("Private/b"),
                                                  QStringLiteral(R"({"amount":12})"), kPw));
        QVERIFY(!whole[QStringLiteral("jobId")].toString().isEmpty());
        awaitJob(p, whole[QStringLiteral("jobId")].toString());
        QVERIFY(slurp(cli + QStringLiteral(".argv")).endsWith(QStringLiteral("--amount\n12\n")));

        const auto frac = parseObj(p.startShield(QStringLiteral("Public/a"),
                                                 QStringLiteral("Private/c"),
                                                 QStringLiteral(R"({"amount":12.5})"), kPw));
        awaitJob(p, frac[QStringLiteral("jobId")].toString());
        const QString argv = slurp(cli + QStringLiteral(".argv"));
        QVERIFY2(!argv.contains(QStringLiteral("--amount\n13")), qPrintable(argv));
    }

    void testValueSpecRefusesATokenWithNoDefinition()
    {
        WalletPlugin p;
        arm(p);
        const auto r = parseObj(p.startShield(QStringLiteral("Public/a"),
                                              QStringLiteral("Private/b"),
                                              QStringLiteral(R"({"asset":"token","amount":"7"})"),
                                              kPw));
        QVERIFY(r[QStringLiteral("error")].toString().contains(QStringLiteral("definitionId")));
    }

    // An unrecognised asset must be an ERROR, never a silent fall-back to native: "toekn"
    // quietly spending LEZ where the user meant a token is a money bug, not a typo.
    void testValueSpecRefusesAnUnknownAsset()
    {
        WalletPlugin p;
        arm(p);
        const auto r = parseObj(p.startShield(QStringLiteral("Public/a"),
                                              QStringLiteral("Private/b"),
                                              QStringLiteral(R"({"asset":"toekn","amount":"7"})"),
                                              kPw));
        QVERIFY(r[QStringLiteral("error")].toString().contains(QStringLiteral("unknown asset")));
    }

    // A caller still passing the OLD argument order - (asset, from, to, amount, …) - puts an
    // account id where the value goes. That has to fail loudly at the door, not reach the CLI.
    void testValueSpecRefusesTheOldArgumentOrder()
    {
        const QString cli = makeRecordingCli(R"({"ok":true})");
        useCli(cli);
        QFile::remove(cli + QStringLiteral(".argv"));
        WalletPlugin p;
        arm(p);
        const auto r = parseObj(p.startShield(QStringLiteral("native"),      // was `asset`
                                              QStringLiteral("Public/a"),    // was `from`
                                              QStringLiteral("Private/b"),   // was `to`
                                              kPw));
        QVERIFY(r.contains(QStringLiteral("error")));
        QVERIFY(!r.contains(QStringLiteral("jobId")));
        QVERIFY2(!QFile::exists(cli + QStringLiteral(".argv")), "a job was started anyway");
    }

    // ── The recipient spec: one argument for "who receives" ──────────────────────────────
    void testForeignRecipientSpecReachesTheCliAsSharedKeys()
    {
        const QString cli = makeRecordingCli(R"({"ok":true,"txId":"t1"})");
        useCli(cli);
        WalletPlugin p;
        arm(p);
        // This is the path that was DEAD at the bridge (7 arguments) and had never once run
        // through it. Folded into startPrivateTransfer, it is 4 arguments and reachable.
        const auto started = parseObj(p.startPrivateTransfer(QStringLiteral("Private/a"),
                                                             kForeignRecipient,
                                                             QStringLiteral("3"), kPw));
        const QString jobId = started[QStringLiteral("jobId")].toString();
        QVERIFY2(!jobId.isEmpty(), qPrintable(QJsonDocument(started).toJson()));
        QCOMPARE(awaitJob(p, jobId)[QStringLiteral("state")].toString(), QString("done"));
        QCOMPARE(slurp(cli + QStringLiteral(".argv")),
                 QString("auth-transfer\nsend\n--from\nPrivate/a\n--to-npk\nnpk\n"
                         "--to-vpk\nvpk\n--to-identifier\nident\n--amount\n3\n"));
        // A foreign recipient is not an account this wallet can credit in local history.
        QCOMPARE(parseObj(p.getJob(jobId))[QStringLiteral("to")].toString(), QString());
    }

    void testOwnedRecipientSpecStillTakesThePrivacyPrefix()
    {
        const QString cli = makeRecordingCli(R"({"ok":true,"txId":"t1"})");
        useCli(cli);
        WalletPlugin p;
        arm(p);
        const auto started = parseObj(p.startPrivateTransfer(QStringLiteral("Private/a"),
                                                             QStringLiteral("bare9"),
                                                             QStringLiteral("3"), kPw));
        const QString jobId = started[QStringLiteral("jobId")].toString();
        QVERIFY(!jobId.isEmpty());
        QCOMPARE(awaitJob(p, jobId)[QStringLiteral("state")].toString(), QString("done"));
        QVERIFY(slurp(cli + QStringLiteral(".argv")).contains(QStringLiteral("--to\nPrivate/bare9")));
    }

    // ── The zone endpoint: one argument for "where the sequencer is" ─────────────────────
    void testZoneEndpointIsOneArgumentForEitherTransport()
    {
        WalletPlugin p;
        const auto clear = parseObj(p.addZone(QStringLiteral("Clear"),
                                              QStringLiteral("example.invalid:3072"), false));
        QCOMPARE(clear[QStringLiteral("ok")].toBool(), true);
        const auto tor = parseObj(p.addZone(QStringLiteral("Onion"),
                                            QStringLiteral("abcd1234.onion"), true));
        QCOMPARE(tor[QStringLiteral("ok")].toBool(), true);

        QHash<QString, QString> endpoints;
        for (const auto& v : parseObj(p.getZones())[QStringLiteral("zones")].toArray())
            endpoints.insert(v.toObject()[QStringLiteral("id")].toString(),
                             v.toObject()[QStringLiteral("endpoint")].toString());
        // A missing scheme still defaults to http:// exactly as it always has.
        QCOMPARE(endpoints.value(clear[QStringLiteral("id")].toString()),
                 QString("http://example.invalid:3072"));
        QCOMPARE(endpoints.value(tor[QStringLiteral("id")].toString()), QString("abcd1234.onion"));

        // The transport is still explicit, so "Tor" plus a clearnet address is still refused
        // rather than silently downgraded to an unprotected clearnet zone.
        QVERIFY(parseObj(p.addZone(QStringLiteral("Bad"), QStringLiteral("https://host:1/"), true))
                    .contains(QStringLiteral("error")));
        QVERIFY(parseObj(p.addZone(QStringLiteral("Bad"), QStringLiteral("abcd.onion"), false))
                    .contains(QStringLiteral("error")));
    }

    // ══ THE PRISTINE-RECIPIENT AUTO-INIT (wallet-wrapper) ════════════════════════════════════
    //
    // On LEZ an auth-transfer to a recipient the chain has never seen is dropped at block-build
    // time with no error anywhere; the CLI then polls 8 blocks (eight minutes at 60s) and
    // reports a not-found that reads like a network fault. `auth-transfer init` is free and
    // needs no proof. These tests drive the REAL wrapper against a stand-in wallet-lez, because
    // the fix has to hold for every outbound transfer that reaches the CLI, whichever verb
    // started it.

    // The shipped wrapper, located from the test binary (the documented build layout is
    // `cmake -B build-tests -S .` inside module/, so scripts/ is one or two levels up).
    QString wrapperPath()
    {
        QDir d(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 5; ++i) {
            const QString p = d.absoluteFilePath(QStringLiteral("scripts/wallet-wrapper"));
            if (QFileInfo(p).isReadable())
                return p;
            if (!d.cdUp())
                break;
        }
        return QString();
    }

    // A stand-in wallet-lez that appends every argv it is given to $RECORD and answers
    // `account get` according to $PRISTINE. $INIT_FAILS makes the registration itself fail.
    QString makeChainCli()
    {
        return writeExec(m_tmp.path() + QStringLiteral("/chain_wallet.sh"), QStringLiteral(
            "#!/bin/sh\n"
            "for a in \"$@\"; do echo \"$a\" >> \"$RECORD\"; done\n"
            "echo '--' >> \"$RECORD\"\n"
            "cat > /dev/null\n"
            "if [ \"$1\" = account ] && [ \"$2\" = get ]; then\n"
            "  if [ \"$PRISTINE\" = 1 ]; then echo 'Account is Uninitialized'\n"
            "  else echo 'Account owned by auth-transfer program'; fi\n"
            "  exit 0\n"
            "fi\n"
            "if [ \"$1\" = auth-transfer ] && [ \"$2\" = init ]; then\n"
            "  if [ \"$INIT_FAILS\" = 1 ]; then echo 'Error: init refused' >&2; exit 1; fi\n"
            "  echo 'Transaction hash is 0xinit'; exit 0\n"
            "fi\n"
            "echo 'Transaction hash is 0xsend'\n"));
    }

    // Run the real wrapper with that stand-in. Returns its stdout; the transcript lands in
    // <record>, one argv token per line, "--" between invocations.
    QString runWrapper(const QStringList& args, bool pristine, const QString& record,
                       bool initFails = false)
    {
        const QString py = trustedPython3();
        const QString wrapper = wrapperPath();
        if (py.isEmpty() || wrapper.isEmpty())
            return QString();
        QFile::remove(record);
        const QString wrapHome = m_tmp.path() + QStringLiteral("/wrapper-home");
        QDir().mkpath(wrapHome);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("WALLET_LEZ"),          makeChainCli());
        env.insert(QStringLiteral("LEE_WALLET_HOME_DIR"), wrapHome);
        env.insert(QStringLiteral("RECORD"),              record);
        env.insert(QStringLiteral("PRISTINE"),            pristine ? QStringLiteral("1")
                                                                   : QStringLiteral("0"));
        env.insert(QStringLiteral("INIT_FAILS"),          initFails ? QStringLiteral("1")
                                                                    : QStringLiteral("0"));
        QProcess proc;
        proc.setProcessEnvironment(env);
        proc.start(py, QStringList{ wrapper } + args);
        if (!proc.waitForStarted(5000))
            return QString();
        proc.closeWriteChannel();          // the wrapper reads the session password off stdin
        proc.waitForFinished(30000);
        return QString::fromUtf8(proc.readAllStandardOutput());
    }

    void testWrapperRegistersAPristineRecipientBeforeSending()
    {
        if (trustedPython3().isEmpty() || wrapperPath().isEmpty())
            QSKIP("python3 or scripts/wallet-wrapper not found from the test binary");
        const QString record = m_tmp.path() + QStringLiteral("/pristine.log");
        const QString out = runWrapper({ QStringLiteral("auth-transfer"), QStringLiteral("send"),
                                         QStringLiteral("--from"), QStringLiteral("Public/a"),
                                         QStringLiteral("--to"),   QStringLiteral("Public/b"),
                                         QStringLiteral("--amount"), QStringLiteral("5") },
                                       true, record);
        QCOMPARE(parseObj(out)[QStringLiteral("ok")].toBool(), true);
        const QString log = slurp(record);
        const int init = log.indexOf(QStringLiteral("auth-transfer\ninit\n--account-id\nPublic/b"));
        const int send = log.indexOf(QStringLiteral("auth-transfer\nsend\n--from"));
        QVERIFY2(init >= 0, qPrintable(QStringLiteral("the recipient was never registered:\n") + log));
        QVERIFY2(send > init, "the transfer went out before the recipient was registered");
    }

    void testWrapperDoesNotRegisterARecipientThatAlreadyExists()
    {
        if (trustedPython3().isEmpty() || wrapperPath().isEmpty())
            QSKIP("python3 or scripts/wallet-wrapper not found from the test binary");
        const QString record = m_tmp.path() + QStringLiteral("/existing.log");
        const QString out = runWrapper({ QStringLiteral("auth-transfer"), QStringLiteral("send"),
                                         QStringLiteral("--from"), QStringLiteral("Public/a"),
                                         QStringLiteral("--to"),   QStringLiteral("Public/b"),
                                         QStringLiteral("--amount"), QStringLiteral("5") },
                                       false, record);
        QCOMPARE(parseObj(out)[QStringLiteral("ok")].toBool(), true);
        const QString log = slurp(record);
        // The common case pays for ONE read and nothing else: no init tx, no extra block wait.
        QVERIFY2(!log.contains(QStringLiteral("auth-transfer\ninit")),
                 qPrintable(QStringLiteral("an already-registered recipient was re-initialised:\n")
                            + log));
        QVERIFY(log.contains(QStringLiteral("account\nget\n--account-id\nPublic/b")));
    }

    // A private destination must stay default-owned: registering one is exactly what makes
    // LEZ v0.2.0 reject the private output, so the auto-init must never touch a shield.
    void testWrapperNeverRegistersAPrivateDestination()
    {
        if (trustedPython3().isEmpty() || wrapperPath().isEmpty())
            QSKIP("python3 or scripts/wallet-wrapper not found from the test binary");
        const QString record = m_tmp.path() + QStringLiteral("/shield.log");
        runWrapper({ QStringLiteral("auth-transfer"), QStringLiteral("send"),
                     QStringLiteral("--from"), QStringLiteral("Public/a"),
                     QStringLiteral("--to"),   QStringLiteral("Private/fresh"),
                     QStringLiteral("--amount"), QStringLiteral("5") }, true, record);
        const QString log = slurp(record);
        QVERIFY2(!log.contains(QStringLiteral("auth-transfer\ninit")),
                 qPrintable(QStringLiteral("a private destination was initialised:\n") + log));
    }

    // BEST EFFORT, NEVER FATAL: if the registration itself fails, the transfer must still go
    // out. This fix can only turn a doomed send into a working one, never the reverse.
    void testWrapperStillSendsWhenTheRegistrationFails()
    {
        if (trustedPython3().isEmpty() || wrapperPath().isEmpty())
            QSKIP("python3 or scripts/wallet-wrapper not found from the test binary");
        const QString record = m_tmp.path() + QStringLiteral("/initfail.log");
        const QString out = runWrapper({ QStringLiteral("auth-transfer"), QStringLiteral("send"),
                                         QStringLiteral("--from"), QStringLiteral("Public/a"),
                                         QStringLiteral("--to"),   QStringLiteral("Public/b"),
                                         QStringLiteral("--amount"), QStringLiteral("5") },
                                       true, record, /*initFails=*/true);
        QCOMPARE(parseObj(out)[QStringLiteral("ok")].toBool(), true);
        QVERIFY(slurp(record).contains(QStringLiteral("auth-transfer\nsend\n--from")));
    }

    // ── surface that had no test at all ────────────────────────────────────────────────
    // A built-in zone is part of the shipped table, not the user's list: removing one would
    // leave the wallet pointing at a zone it cannot describe. Only user-added zones go.
    void testOnlyAUserAddedZoneCanBeRemovedAndTheActiveOneFallsBack()
    {
        useCli(makeFakeCli(QStringLiteral("{\"ok\":true}")));
        WalletPlugin p;
        arm(p);
        for (const QString& builtin : { QStringLiteral("devnet"),
                                        QStringLiteral("paradox-clearnet"),
                                        QStringLiteral("logos-testnet") })
            QVERIFY2(parseObj(p.removeZone(builtin)).contains(QStringLiteral("error")),
                     qPrintable(QStringLiteral("built-in zone %1 was removable").arg(builtin)));

        const QString id = useUserZone(p);              // added AND active
        QCOMPARE(parseObj(p.removeZone(id))[QStringLiteral("ok")].toBool(), true);
        // the active zone just vanished: the wallet must not be left pointing at it
        QVERIFY2(parseObj(p.getNetwork())[QStringLiteral("id")].toString() != id,
                 "the removed zone was still active");
        for (const auto& v : parseArr(p.getZones()))
            QVERIFY2(v.toObject()[QStringLiteral("id")].toString() != id, "zone still listed");
    }

    // getTokens is the per-account display path - the one that showed every account the same
    // balance. The account id must reach the wallet verbatim, and an empty one must not run
    // anything at all (the UI calls this on every account switch, including before one is set).
    void testGetTokensAsksTheWalletForExactlyThatAccount()
    {
        const QString cli = makeRecordingCli(QStringLiteral("[]"));
        useCli(cli);
        WalletPlugin p;
        arm(p);
        p.getTokens(QStringLiteral("Public/account-B"));
        const QString argv = slurp(cli + QStringLiteral(".argv"));
        QVERIFY2(argv.contains(QStringLiteral("tokens\nPublic/account-B")), qPrintable(argv));
        QVERIFY2(!argv.contains(QStringLiteral("account-A")), qPrintable(argv));

        QFile::remove(cli + QStringLiteral(".argv"));
        QCOMPARE(p.getTokens(QString()), QString("[]"));
        QVERIFY2(!QFile::exists(cli + QStringLiteral(".argv")),
                 "getTokens(\"\") ran the wallet");
    }

    // initAccount is what makes a pristine recipient able to receive: it must name the account
    // and refuse an empty one rather than initialising whatever the CLI defaults to.
    void testInitAccountInitialisesTheNamedAccountOnly()
    {
        const QString cli = makeRecordingCli(QStringLiteral("{\"ok\":true}"));
        useCli(cli);
        WalletPlugin p;
        arm(p);
        QCOMPARE(parseObj(p.initAccount(QStringLiteral("  Public/fresh  ")))
                     [QStringLiteral("ok")].toBool(), true);
        const QString argv = slurp(cli + QStringLiteral(".argv"));
        QVERIFY2(argv.contains(QStringLiteral("auth-transfer\ninit\n--account-id\nPublic/fresh")),
                 qPrintable(argv));

        QFile::remove(cli + QStringLiteral(".argv"));
        QVERIFY(parseObj(p.initAccount(QStringLiteral("   "))).contains(QStringLiteral("error")));
        QVERIFY2(!QFile::exists(cli + QStringLiteral(".argv")), "an empty id still ran init");
    }

    // A label is local UI state, so it must survive into listAccounts and be removable.
    void testAnAccountNameIsAppliedToTheListingAndClearedByAnEmptyName()
    {
        useCli(makeFakeCli(QStringLiteral("[{\"id\":\"Public/a\",\"type\":\"public\"}]")));
        WalletPlugin p;
        arm(p);
        QCOMPARE(parseObj(p.setAccountName(QStringLiteral("Public/a"), QStringLiteral("Salary")))
                     [QStringLiteral("ok")].toBool(), true);
        // listAccounts surfaces the user's label as "name", falling back to whatever the
        // wallet itself reports when the user has not named the account.
        QCOMPARE(parseArr(p.listAccounts()).at(0).toObject()[QStringLiteral("name")].toString(),
                 QString("Salary"));

        p.setAccountName(QStringLiteral("Public/a"), QString());
        const QJsonObject cleared = parseArr(p.listAccounts()).at(0).toObject();
        QVERIFY2(cleared[QStringLiteral("name")].toString().isEmpty(),
                 "an emptied name still labelled the account");
        QVERIFY(parseObj(p.setAccountName(QString(), QStringLiteral("x")))
                    .contains(QStringLiteral("error")));
    }

    // Transaction history is per account and starts empty - never another account's list.
    void testTransactionHistoryIsPerAccountAndStartsEmpty()
    {
        useCli(makeFakeCli(QStringLiteral("[]")));
        WalletPlugin p;
        arm(p);
        QCOMPARE(parseArr(p.getTransactions(QStringLiteral("Public/a"))).size(), 0);
        QVERIFY(parseObj(p.getTransactions(QString())).contains(QStringLiteral("error")));
    }

    // The private-note scan is a background job: a second start must not spawn a second
    // scanner, and the status verb must say so rather than reporting a fresh run.
    void testASecondPrivateSyncNeverStartsASecondScanner()
    {
        // a CLI that stays alive long enough for the second call to see it running
        const QString cli = m_tmp.path() + QStringLiteral("/slowsync.sh");
        writeExec(cli, QStringLiteral("#!/bin/sh\nread pw\nsleep 2\necho '{\"ok\":true}'\n"));
        WalletPlugin p;
        arm(p);
        useCli(cli);
        QCOMPARE(parseObj(p.syncPrivateStatus())[QStringLiteral("running")].toBool(), false);
        p.startSyncPrivate();
        QCOMPARE(parseObj(p.syncPrivateStatus())[QStringLiteral("running")].toBool(), true);
        QCOMPARE(parseObj(p.startSyncPrivate())[QStringLiteral("alreadyRunning")].toBool(), true);
    }

    // Rejecting is a state machine, not a free-for-all: only a pending ZONE request can be
    // rejected, and never twice.
    void testRejectZoneOnlyHandlesAPendingZoneRequestAndOnlyOnce()
    {
        useCli(makeFakeCli(QStringLiteral("{\"ok\":true}")));
        WalletPlugin p;
        arm(p);
        QVERIFY(parseObj(p.rejectZone(QStringLiteral("no-such-request")))
                    .contains(QStringLiteral("error")));

        // a CONNECT request is not a zone request.
        // NOTE the escaped string: moc's lexer treats "//" as a comment even INSIDE a raw
        // string literal, so R"(...https://...)" swallows the closing delimiter, the class
        // parse collapses, and moc emits "No relevant classes found" - an EMPTY .moc, which
        // surfaces only as `undefined reference to vtable for TestWalletPlugin` at link time.
        // Any URL in this file must be written escaped, never as a raw string.
        const auto conn = parseObj(p.connectRequest(
            QStringLiteral("{\"appName\":\"App\",\"origin\":\"https://app.invalid\"}"),
            QStringLiteral("[\"accounts\"]")));
        const QString cid = conn[QStringLiteral("requestId")].toString();
        QVERIFY(!cid.isEmpty());
        const auto wrong = parseObj(p.rejectZone(cid));
        QVERIFY2(wrong.contains(QStringLiteral("error")), qPrintable(QJsonDocument(wrong).toJson()));
        QCOMPARE(wrong[QStringLiteral("error")].toString(), QString("not a zone request"));

        // and the connect request itself is still pending, not collaterally damaged: its own
        // reject verb still works, and only once.
        QCOMPARE(parseObj(p.rejectConnect(cid))[QStringLiteral("ok")].toBool(), true);
        QVERIFY(parseObj(p.rejectConnect(cid)).contains(QStringLiteral("error")));
    }

    // addToken forwards to the registry and refuses an empty definition.
    void testAddTokenForwardsTheDefinitionAndRefusesAnEmptyOne()
    {
        const QString cli = makeRecordingCli(QStringLiteral("{\"ok\":true}"));
        useCli(cli);
        WalletPlugin p;
        arm(p);
        p.addToken(QStringLiteral("  DefX  "));
        QVERIFY2(slurp(cli + QStringLiteral(".argv"))
                     .contains(QStringLiteral("token-registry\nadd\nDefX")),
                 qPrintable(slurp(cli + QStringLiteral(".argv"))));
        QFile::remove(cli + QStringLiteral(".argv"));
        QVERIFY(parseObj(p.addToken(QString())).contains(QStringLiteral("error")));
        QVERIFY2(!QFile::exists(cli + QStringLiteral(".argv")), "an empty definition still ran");
    }
};

QTEST_MAIN(TestWalletPlugin)
#include "test_wallet_plugin.moc"

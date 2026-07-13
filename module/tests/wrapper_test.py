#!/usr/bin/env python3
"""Offline tests for module/scripts/wallet-wrapper against tests/fake-wallet-lez.

Covers the token-vault surface added in 5ef0c25/02efeb2: the merged `tokens` view,
consolidate, vault_for designation precedence, split-source token-transfer, the
one-shot private-recipient guard (+ its recording), token-shield sourcing/auto-top-up,
direct-holdings, token-registry validation, and the legacy registry migration.

Run:  python3 module/tests/wrapper_test.py     (no network, no chain, ~seconds)
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
WRAPPER = os.path.join(HERE, "..", "scripts", "wallet-wrapper")
FAKE = os.path.join(HERE, "fake-wallet-lez")
B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

DEF = "DefAccount111111111111111111111111111111"
SUP = "SupAccount111111111111111111111111111111"
VAULT = "VaultAccount1111111111111111111111111111"
OWNER = "OwnerAccount1111111111111111111111111111"
RECIP = "RecipAccount1111111111111111111111111111"
PRIV = "Private/PrivAccount11111111111111111111111111111"


def ata_addr(owner, defb):
    h = hashlib.md5((owner + ":" + defb).encode()).hexdigest()
    return "".join(B58[int(c, 16) % len(B58)] for c in (h + h)[:44])


class WrapperTest(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.mkdtemp(prefix="wrap-home-")
        self.treasury = tempfile.mkdtemp(prefix="wrap-treas-")
        self.chain = os.path.join(self.home, "fake_chain.json")
        with open(os.path.join(self.home, "wallet_config.json"), "w") as f:
            json.dump({"sequencer_addr": "http://fake:1/", "zone": "testzone"}, f)
        self.reg_path = os.path.join(self.home, "token_registry-testzone.json")
        self.env = dict(
            os.environ,
            WALLET_LEZ=FAKE,
            LEE_WALLET_HOME_DIR=self.home,
            MEDUSA_TREASURY_HOME=self.treasury,
            FAKE_CHAIN=self.chain,
        )

    def tearDown(self):
        shutil.rmtree(self.home, ignore_errors=True)
        shutil.rmtree(self.treasury, ignore_errors=True)

    # ── helpers ────────────────────────────────────────────────────────────
    def seed_chain(self, chain, accounts=None):
        with open(self.chain, "w") as f:
            json.dump({"accounts": accounts or [], "chain": chain, "seq": 0}, f)

    def seed_reg(self, **kw):
        reg = {"definitions": [], "names": {}, "vaults": {}, "privateDests": []}
        reg.update(kw)
        with open(self.reg_path, "w") as f:
            json.dump(reg, f)

    def reg(self):
        with open(self.reg_path) as f:
            return json.load(f)

    def wrap(self, *args):
        p = subprocess.run([sys.executable, WRAPPER, *args], capture_output=True,
                           text=True, input="\n", env=self.env, timeout=120)
        try:
            return p.returncode, json.loads(p.stdout.strip())
        except (json.JSONDecodeError, ValueError):
            return p.returncode, {"_raw": p.stdout, "_err": p.stderr}

    def std_state(self, vault_bal=40, ata_bal=60):
        """DEF definition + user vault + owner ATA, registry knowing all of it."""
        self.seed_chain({
            DEF: {"kind": "definition", "name": "TOK", "supply": 1000},
            VAULT: {"kind": "holding", "def": DEF, "balance": vault_bal},
            ata_addr(OWNER, DEF): {"kind": "holding", "def": DEF, "balance": ata_bal,
                                    "pda": True},
        }, accounts=[
            {"id": "Public/" + OWNER, "type": "public", "balance": 5,
             "initialized": True, "label": None},
            {"id": "Public/" + VAULT, "type": "public", "balance": 0,
             "initialized": True, "label": None},
            {"id": PRIV, "type": "private", "balance": 0,
             "initialized": False, "label": None},
        ])
        self.seed_reg(definitions=[DEF], names={DEF: "TOK"}, vaults={DEF: VAULT})

    # ── tests ──────────────────────────────────────────────────────────────
    def test_tokens_merges_ata_and_vault(self):
        self.std_state(vault_bal=40, ata_bal=60)
        rc, out = self.wrap("tokens", "Public/" + OWNER)
        self.assertEqual(rc, 0)
        self.assertEqual(out[0]["balance"], "100")
        self.assertEqual(out[0]["ataBalance"], "60")
        self.assertEqual(out[0]["vaultBalance"], "40")

    def test_direct_holdings_lists_vault_with_ata_total(self):
        self.std_state(vault_bal=40, ata_bal=60)
        rc, out = self.wrap("direct-holdings")
        self.assertEqual(rc, 0)
        accounts = {h["account"]: h for h in out}
        self.assertIn(VAULT, accounts)
        self.assertEqual(accounts[VAULT]["balance"], "40")
        self.assertEqual(accounts[VAULT]["ataTotal"], "60")
        self.assertEqual(accounts[VAULT]["ticker"], "TOK")

    def test_consolidate_moves_ata_into_vault(self):
        self.std_state(vault_bal=40, ata_bal=60)
        rc, out = self.wrap("consolidate", "Public/" + OWNER, DEF)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out["moved"], 60)
        self.assertEqual(out["vaultBalance"], "100")
        rc, out = self.wrap("tokens", "Public/" + OWNER)
        self.assertEqual(out[0]["ataBalance"], "0")
        self.assertEqual(out[0]["vaultBalance"], "100")

    def test_consolidate_refuses_without_initialized_vault(self):
        self.std_state(vault_bal=40, ata_bal=60)
        self.seed_reg(definitions=[DEF], names={DEF: "TOK"}, vaults={})
        # remove the vault holding so no initialized direct holding exists at all
        with open(self.chain) as f:
            st = json.load(f)
        del st["chain"][VAULT]
        with open(self.chain, "w") as f:
            json.dump(st, f)
        rc, out = self.wrap("consolidate", "Public/" + OWNER, DEF)
        self.assertEqual(rc, 1)
        self.assertIn("no initialized vault", out["error"])

    def test_vault_for_prefers_initialized_over_recorded_pristine(self):
        self.std_state(vault_bal=40, ata_bal=60)
        # record a PRISTINE (uninitialized) vault; the real one must win
        self.seed_reg(definitions=[DEF], names={DEF: "TOK"},
                      vaults={DEF: "PristineRecorded111111111111111111111111"})
        rc, out = self.wrap("consolidate", "Public/" + OWNER, DEF)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out["vault"], VAULT)
        self.assertEqual(self.reg()["vaults"][DEF], VAULT)  # re-designated

    def test_token_transfer_splits_across_vault_and_ata(self):
        self.std_state(vault_bal=30, ata_bal=50)
        rc, out = self.wrap("token-transfer", "Public/" + OWNER, "Public/" + RECIP,
                            DEF, "70")
        self.assertEqual(rc, 0, out)
        with open(self.chain) as f:
            st = json.load(f)
        self.assertEqual(st["chain"][ata_addr(RECIP, DEF)]["balance"], 70)
        self.assertEqual(st["chain"][VAULT]["balance"], 0)          # 30 + 40 moved in, 70 out
        self.assertEqual(st["chain"][ata_addr(OWNER, DEF)]["balance"], 10)

    def test_token_transfer_insufficient_is_a_clean_error(self):
        self.std_state(vault_bal=10, ata_bal=5)
        rc, out = self.wrap("token-transfer", "Public/" + OWNER, "Public/" + RECIP,
                            DEF, "70")
        self.assertEqual(rc, 1)
        self.assertIn("insufficient token balance: vault 10 + ATA 5 < 70", out["error"])

    def test_one_shot_guard_blocks_recorded_private_dest(self):
        self.std_state()
        self.seed_reg(definitions=[DEF], names={DEF: "TOK"}, vaults={DEF: VAULT},
                      privateDests=[PRIV])
        rc, out = self.wrap("token-shield", "Public/" + OWNER, PRIV, DEF, "5")
        self.assertEqual(rc, 1)
        self.assertIn("one-shot", out["error"])
        # the raw passthrough send is guarded too
        rc, out = self.wrap("token", "send", "--from", "Public/" + VAULT,
                            "--to", PRIV, "--amount", "5")
        self.assertEqual(rc, 1)
        self.assertIn("one-shot", out["error"])

    def test_token_shield_records_dest_and_spends_vault(self):
        self.std_state(vault_bal=40)
        rc, out = self.wrap("token-shield", "Public/" + OWNER, PRIV, DEF, "5")
        self.assertEqual(rc, 0, out)
        self.assertIn(PRIV, self.reg()["privateDests"])
        with open(self.chain) as f:
            st = json.load(f)
        self.assertEqual(st["chain"][VAULT]["balance"], 35)

    def test_token_shield_auto_tops_up_vault_from_ata(self):
        self.std_state(vault_bal=10, ata_bal=60)
        rc, out = self.wrap("token-shield", "Public/" + OWNER, PRIV, DEF, "50")
        self.assertEqual(rc, 0, out)
        with open(self.chain) as f:
            st = json.load(f)
        # 40 consolidated in (10+40=50), 50 shielded out
        self.assertEqual(st["chain"][VAULT]["balance"], 0)
        self.assertEqual(st["chain"][ata_addr(OWNER, DEF)]["balance"], 20)

    def test_token_shield_short_error_names_the_balance(self):
        self.std_state(vault_bal=10, ata_bal=0)
        rc, out = self.wrap("token-shield", "Public/" + OWNER, PRIV, DEF, "50")
        self.assertEqual(rc, 1)
        self.assertIn("has only 10", out["error"])

    def test_token_registry_add_validates(self):
        self.std_state()
        rc, out = self.wrap("token-registry", "add", "not-base58-!!")
        self.assertEqual(rc, 1)
        self.assertIn("not a valid token id", out["error"])
        rc, out = self.wrap("token-registry", "add",
                            "Unknown11111111111111111111111111111111")
        self.assertEqual(rc, 1)
        self.assertIn("no token with this id", out["error"])

    def test_token_registry_lists_vaults_and_private_dests(self):
        self.std_state()
        self.seed_reg(definitions=[DEF], names={DEF: "TOK"}, vaults={DEF: VAULT},
                      privateDests=[PRIV])
        rc, out = self.wrap("token-registry")
        self.assertEqual(out["vaults"][DEF], VAULT)
        self.assertEqual(out["privateDests"], [PRIV])

    def test_legacy_registry_migrates_to_devnet(self):
        self.seed_chain({})
        with open(os.path.join(self.home, "wallet_config.json"), "w") as f:
            json.dump({"sequencer_addr": "http://127.0.0.1:3071/"}, f)  # no zone key
        legacy = {"definitions": [DEF], "names": {DEF: "OLD"}}
        with open(os.path.join(self.home, "token_registry.json"), "w") as f:
            json.dump(legacy, f)
        self.wrap("token-registry")
        self.assertFalse(os.path.exists(os.path.join(self.home, "token_registry.json")))
        with open(os.path.join(self.home, "token_registry-devnet.json")) as f:
            self.assertEqual(json.load(f)["names"][DEF], "OLD")


if __name__ == "__main__":
    unittest.main(verbosity=2)

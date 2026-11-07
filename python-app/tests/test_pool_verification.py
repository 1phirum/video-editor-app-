"""Antigravity readiness requires service access, not just Google OAuth."""
import copy
import io
import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import MagicMock, patch
import urllib.error

sys.path.insert(0, os.environ.get("CUTPRO_POOL_TEST_TOOLS",
                                str(Path(__file__).resolve().parents[1] / "tools")))
import pool_tool as pool


class PoolVerificationTests(unittest.TestCase):
    def test_profile_errors_are_not_treated_as_success(self):
        with patch.object(pool, "http_json", side_effect=RuntimeError("Google connection failed")):
            with self.assertRaises(RuntimeError):
                pool.detect_profile("fixture-token")

    def test_profile_reads_active_antigravity_tier(self):
        response = {"currentTier": {"id": "free-tier"},
                    "allowedTiers": [{"id": "standard-tier", "isDefault": True}],
                    "cloudaicompanionProject": {"id": "fixture-project"}}
        with patch.object(pool, "http_json", return_value=response):
            self.assertEqual(pool.detect_profile("fixture-token"), ("free", "fixture-project"))

    def test_profile_without_project_needs_antigravity_setup(self):
        with patch.object(pool, "http_json", return_value={"allowedTiers": [{"id": "free-tier"}]}):
            with self.assertRaises(RuntimeError):
                pool.detect_profile("fixture-token")

    def test_provider_verification_url_survives_long_error_details(self):
        url = "https://accounts.google.com/signin/continue?fixture=verification"
        error = {"error": {"message": "To use this service, please verify your account.",
                           "details": [{"padding": "x" * 300},
                                       {"reason": "VALIDATION_REQUIRED", "metadata": {"validation_url": url}}]}}
        response = urllib.error.HTTPError("https://fixture.invalid", 403, "Forbidden", {},
                                          io.BytesIO(json.dumps(error).encode()))
        with patch.object(pool.urllib.request, "urlopen", side_effect=response):
            with self.assertRaises(pool.GoogleRequestError) as failure:
                pool.http_json("https://fixture.invalid")
        self.assertTrue(failure.exception.verification_required)
        self.assertEqual(failure.exception.verification_url, url)

    def test_verification_urls_only_open_official_https_sites(self):
        for url in ("http://accounts.google.com/check", "https://accounts.google.com.evil.invalid/check",
                    "file:///C:/secret", "https://user:pass@accounts.google.com/check"):
            with self.subTest(url=url):
                error = pool.GoogleRequestError(403, {"message": "verify your account", "details": [
                    {"metadata": {"validation_url": url}}]})
                self.assertEqual(error.verification_url, pool.ANTIGRAVITY_SETUP_URL)

    def verify(self, failure=None, account_id="one"):
        document = {"version": 1, "accounts": [
            {"id": "one", "email": "one@example.invalid", "enabled": False, "status": "unverified"},
            {"id": "two", "email": "two@example.invalid", "enabled": True, "status": "error"}]}
        def update(account_id, operation):
            operation(next(a for a in document["accounts"] if a["id"] == account_id))
            return copy.deepcopy(document)
        with patch.object(pool, "load_store", side_effect=lambda: copy.deepcopy(document)), \
                patch.object(pool, "update_account", side_effect=update), \
                patch.object(pool, "write_state"), patch.object(pool, "emit"), \
                patch.object(pool, "refresh_access_token", return_value="fixture-token"), \
                patch.object(pool, "detect_profile", return_value=("free", "fixture-project")), \
                patch.object(pool, "account_models", return_value=(["gemini-2.5-flash"], "fixture-project")), \
                patch.object(pool, "http_json", side_effect=failure,
                             return_value={"response": {"candidates": [{"finishReason": "STOP"}]}}) as send:
            self.assertEqual(pool.command_verify(account_id), 0)
        return document, send

    def test_ready_requires_successful_generation_probe_and_preserves_selection(self):
        document, send = self.verify()
        self.assertEqual(document["accounts"][0]["status"], "ready")
        self.assertFalse(document["accounts"][0]["enabled"])
        self.assertEqual(document["accounts"][1]["status"], "error")
        self.assertEqual(send.call_count, 1)
        body = send.call_args.args[1]
        self.assertEqual(body["model"], "gemini-2.5-flash")
        self.assertEqual(body["request"]["generationConfig"]["maxOutputTokens"], 8)

    def test_failed_service_probe_is_not_ready_and_has_setup_action(self):
        failure = pool.GoogleRequestError(403, {"message": "verify your account"})
        document, send = self.verify(failure)
        account = document["accounts"][0]
        self.assertEqual(account["status"], "unverified")
        self.assertEqual(account["verificationUrl"], pool.ANTIGRAVITY_SETUP_URL)
        self.assertIn("Antigravity", account["statusDetail"])
        self.assertFalse(account["enabled"])
        self.assertEqual(send.call_count, 1)

    def test_verify_all_checks_every_account_once_without_enabling_them(self):
        document, send = self.verify(account_id=None)
        self.assertTrue(all(a["status"] == "ready" for a in document["accounts"]))
        self.assertFalse(document["accounts"][0]["enabled"])
        self.assertTrue(document["accounts"][1]["enabled"])
        self.assertEqual(send.call_count, 2)

    def test_signin_preserves_session_when_antigravity_setup_is_required(self):
        document = {"version": 1, "accounts": []}
        server = MagicMock()
        server.server_address = ("127.0.0.1", 12345)
        def emit(event):
            if "authUrl" in event:
                server.result = "fixture-code"
        def update(operation):
            operation(document)
            return document
        tokens = io.BytesIO(json.dumps({"access_token": "fixture-access", "refresh_token": "fixture-refresh"}).encode())
        with patch.object(pool, "ThreadingHTTPServer", return_value=server), \
                patch.object(pool, "load_store", return_value=document), \
                patch.object(pool, "update_store", side_effect=update), \
                patch.object(pool, "write_state"), patch.object(pool, "emit", side_effect=emit), \
                patch.object(pool.urllib.request, "urlopen", return_value=tokens), \
                patch.object(pool, "http_json", return_value={"email": "fixture@example.invalid"}), \
                patch.object(pool, "detect_profile", side_effect=pool.GoogleRequestError(403, {"message": "verify your account"})):
            self.assertEqual(pool.sign_in(), 0)
        self.assertEqual(len(document["accounts"]), 1)
        account = document["accounts"][0]
        self.assertEqual(account["refreshToken"], "fixture-refresh")
        self.assertEqual(account["status"], "unverified")
        self.assertEqual(account["verificationUrl"], pool.ANTIGRAVITY_SETUP_URL)


if __name__ == "__main__":
    unittest.main()

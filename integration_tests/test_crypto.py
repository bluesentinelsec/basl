#!/usr/bin/env python3
"""Integration tests for VIGIL crypto module."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str) -> tuple[int, str, str]:
    with tempfile.TemporaryDirectory(prefix="vigil_crypto_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True, text=True, timeout=10,
        )
        return result.returncode, result.stdout, result.stderr


class Sha256Test(unittest.TestCase):
    def test_sha256_empty(self):
        code = '''import "crypto"
fn main() -> i32 {
    string h, err e = crypto.sha256("")
    if e != ok { return 2 }
    if h == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_sha256_hello(self):
        code = '''import "crypto"
fn main() -> i32 {
    string h, err e = crypto.sha256("hello")
    if e != ok { return 2 }
    if h == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class Sha512Test(unittest.TestCase):
    def test_sha512_hello(self):
        code = '''import "crypto"
fn main() -> i32 {
    string h, err e = crypto.sha512("hello")
    if e != ok { return 2 }
    if h.starts_with("9b71d224bd62f3785d96d46ad3ea3d73") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class HmacTest(unittest.TestCase):
    def test_hmac_sha256(self):
        code = '''import "crypto"
fn main() -> i32 {
    string h, err e = crypto.hmac_sha256("key", "message")
    if e != ok { return 2 }
    if h == "6e9ef29b75fffc5b7abae527d58fdadb2fe42e7219011976917343065f58ed4a" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class HexTest(unittest.TestCase):
    def test_hex_encode(self):
        code = '''import "crypto"
fn main() -> i32 {
    string h, err e = crypto.hex_encode("hello")
    if e != ok { return 2 }
    if h == "68656c6c6f" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_hex_decode(self):
        code = '''import "crypto"
fn main() -> i32 {
    string d, err e = crypto.hex_decode("68656c6c6f")
    if e != ok { return 2 }
    if d == "hello" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_hex_roundtrip(self):
        code = '''import "crypto"
fn main() -> i32 {
    string orig = "test data"
    string hex, err e1 = crypto.hex_encode(orig)
    if e1 != ok { return 2 }
    string back, err e2 = crypto.hex_decode(hex)
    if e2 != ok { return 3 }
    if back == orig { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class Base64Test(unittest.TestCase):
    def test_base64_encode(self):
        code = '''import "crypto"
fn main() -> i32 {
    string b, err e = crypto.base64_encode("hello")
    if e != ok { return 2 }
    if b == "aGVsbG8=" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_base64_decode(self):
        code = '''import "crypto"
fn main() -> i32 {
    string d, err e = crypto.base64_decode("aGVsbG8=")
    if e != ok { return 2 }
    if d == "hello" { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_base64_roundtrip(self):
        code = '''import "crypto"
fn main() -> i32 {
    string orig = "test data 123"
    string b64, err e1 = crypto.base64_encode(orig)
    if e1 != ok { return 2 }
    string back, err e2 = crypto.base64_decode(b64)
    if e2 != ok { return 3 }
    if back == orig { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class RandomBytesTest(unittest.TestCase):
    def test_random_bytes_length(self):
        code = '''import "crypto"
fn main() -> i32 {
    string r, err e1 = crypto.random_bytes(32)
    if e1 != ok { return 2 }
    string hex, err e2 = crypto.hex_encode(r)
    if e2 != ok { return 3 }
    // 32 bytes = 64 hex chars
    if hex.len() == 64 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_random_bytes_different(self):
        code = '''import "crypto"
fn main() -> i32 {
    string r1, err e1 = crypto.random_bytes(16)
    string r2, err e2 = crypto.random_bytes(16)
    if e1 != ok || e2 != ok { return 2 }
    if r1 != r2 { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class ConstantTimeEqTest(unittest.TestCase):
    def test_equal(self):
        code = '''import "crypto"
fn main() -> i32 {
    if crypto.constant_time_eq("hello", "hello") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_not_equal(self):
        code = '''import "crypto"
fn main() -> i32 {
    if !crypto.constant_time_eq("hello", "world") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class EncryptDecryptTest(unittest.TestCase):
    def test_roundtrip(self):
        code = '''import "crypto"
fn main() -> i32 {
    string key, err e1 = crypto.random_bytes(32)
    string nonce, err e2 = crypto.random_bytes(12)
    if e1 != ok || e2 != ok { return 2 }
    string plaintext = "secret message"
    string encrypted, err e3 = crypto.encrypt(key, nonce, plaintext, "")
    if e3 != ok { return 3 }
    string decrypted, err e4 = crypto.decrypt(key, encrypted, "")
    if e4 != ok { return 4 }
    if decrypted == plaintext { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_wrong_key_fails(self):
        code = '''import "crypto"
fn main() -> i32 {
    string key1, err e1 = crypto.random_bytes(32)
    string key2, err e2 = crypto.random_bytes(32)
    string nonce, err e3 = crypto.random_bytes(12)
    if e1 != ok || e2 != ok || e3 != ok { return 2 }
    string encrypted, err e4 = crypto.encrypt(key1, nonce, "secret", "")
    if e4 != ok { return 3 }
    string decrypted, err e5 = crypto.decrypt(key2, encrypted, "")
    if e5 != ok { return 0 }  // Should fail auth -> error
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class Pbkdf2Test(unittest.TestCase):
    def test_pbkdf2_known_vector(self):
        code = '''import "crypto"
fn main() -> i32 {
    string key, err e = crypto.pbkdf2("password", "salt", 1, 20)
    if e != ok { return 2 }
    // Known test vector for PBKDF2-SHA256
    if key.starts_with("120fb6cf") { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class PasswordEncryptTest(unittest.TestCase):
    def test_password_roundtrip(self):
        code = '''import "crypto"
fn main() -> i32 {
    string plaintext = "hello world"
    string encrypted, err e1 = crypto.password_encrypt("my secret password", plaintext)
    if e1 != ok { return 2 }
    string decrypted, err e2 = crypto.password_decrypt("my secret password", encrypted)
    if e2 != ok { return 3 }
    if decrypted != plaintext { return 1 }
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_wrong_password_fails(self):
        code = '''import "crypto"
fn main() -> i32 {
    string encrypted, err e1 = crypto.password_encrypt("correct password", "secret")
    if e1 != ok { return 2 }
    string decrypted, err e2 = crypto.password_decrypt("wrong password", encrypted)
    if e2 != ok { return 0 }  // Should fail -> error
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()

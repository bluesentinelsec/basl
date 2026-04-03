#!/usr/bin/env python3
"""Integration tests for VIGIL net module."""

import os
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

VIGIL_BIN = os.environ.get("VIGIL_BIN", "./build/vigil")


def run_vigil(code: str, timeout: int = 10) -> tuple[int, str, str]:
    """Run VIGIL code and return (exit_code, stdout, stderr)."""
    with tempfile.TemporaryDirectory(prefix="vigil_net_") as tmpdir:
        path = Path(tmpdir) / "test.vigil"
        path.write_text(code)
        result = subprocess.run(
            [VIGIL_BIN, "run", str(path)],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return result.returncode, result.stdout, result.stderr


def run_vigil_async(code: str, tmpdir: str) -> subprocess.Popen:
    """Run VIGIL code asynchronously."""
    path = Path(tmpdir) / "test.vigil"
    path.write_text(code)
    return subprocess.Popen(
        [VIGIL_BIN, "run", str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


class TcpListenTest(unittest.TestCase):
    """Tests for net.tcp_listen()"""

    def test_tcp_listen_returns_handle(self):
        """tcp_listen returns non-negative handle on success."""
        code = '''import "net"
fn main() -> i32 {
    i64 server = net.tcp_listen("127.0.0.1", 19001)
    if server >= i64(0) {
        net.close(server)
        return 0
    }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class TcpConnectTest(unittest.TestCase):
    """Tests for net.tcp_connect()"""

    def test_tcp_connect_refused(self):
        """tcp_connect to closed port returns -1."""
        code = '''import "net"
fn main() -> i32 {
    i64 sock = net.tcp_connect("127.0.0.1", 19999)
    if sock < i64(0) {
        return 0;  // Expected - no server
    }
    net.close(sock)
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class TcpClientServerTest(unittest.TestCase):
    """Tests for TCP client/server communication."""

    def test_tcp_echo(self):
        """TCP server can accept connection and echo data."""
        with tempfile.TemporaryDirectory(prefix="vigil_tcp_") as tmpdir:
            server_code = '''import "net"
import "fmt"
fn main() -> i32 {
    i64 server = net.tcp_listen("127.0.0.1", 19002)
    if server < i64(0) { return 1 }
    
    i64 client = net.tcp_accept(server)
    if client < i64(0) { 
        net.close(server)
        return 2 
    }
    
    string data = net.read(client, 1024)
    i32 sent = net.write(client, data)
    
    net.close(client)
    net.close(server)
    
    if sent > 0 { return 0 }
    return 3
}'''
            client_code = '''import "net"
import "time"
fn main() -> i32 {
    time.sleep(i64(100))
    
    i64 sock = net.tcp_connect("127.0.0.1", 19002)
    if sock < i64(0) { return 1 }
    
    net.write(sock, "hello")
    string response = net.read(sock, 1024)
    net.close(sock)
    
    if response == "hello" { return 0 }
    return 2
}'''
            # Start server
            server_path = Path(tmpdir) / "server.vigil"
            server_path.write_text(server_code)
            server_proc = subprocess.Popen(
                [VIGIL_BIN, "run", str(server_path)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

            time.sleep(0.2)  # Let server start

            # Run client
            client_path = Path(tmpdir) / "client.vigil"
            client_path.write_text(client_code)
            client_result = subprocess.run(
                [VIGIL_BIN, "run", str(client_path)],
                capture_output=True,
                text=True,
                timeout=5,
            )

            server_proc.wait(timeout=5)

            self.assertEqual(client_result.returncode, 0, 
                           f"client stderr: {client_result.stderr}")
            self.assertEqual(server_proc.returncode, 0)


class TcpReadWriteTest(unittest.TestCase):
    """Tests for net.read() and net.write()"""

    def test_write_returns_bytes_sent(self):
        """write returns number of bytes sent."""
        with tempfile.TemporaryDirectory(prefix="vigil_rw_") as tmpdir:
            server_code = '''import "net"
fn main() -> i32 {
    i64 server = net.tcp_listen("127.0.0.1", 19003)
    i64 client = net.tcp_accept(server)
    string data = net.read(client, 1024)
    net.close(client)
    net.close(server)
    return 0
}'''
            client_code = '''import "net"
import "time"
fn main() -> i32 {
    time.sleep(i64(100))
    i64 sock = net.tcp_connect("127.0.0.1", 19003)
    if sock < i64(0) { return 1 }
    
    i32 sent = net.write(sock, "test data")
    net.close(sock)
    
    if sent == 9 { return 0 }
    return 2
}'''
            server_path = Path(tmpdir) / "server.vigil"
            server_path.write_text(server_code)
            server_proc = subprocess.Popen(
                [VIGIL_BIN, "run", str(server_path)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            time.sleep(0.2)

            client_path = Path(tmpdir) / "client.vigil"
            client_path.write_text(client_code)
            client_result = subprocess.run(
                [VIGIL_BIN, "run", str(client_path)],
                capture_output=True,
                text=True,
                timeout=5,
            )

            server_proc.wait(timeout=5)
            self.assertEqual(client_result.returncode, 0,
                           f"stderr: {client_result.stderr}")


class UdpBindTest(unittest.TestCase):
    """Tests for net.udp_bind()"""

    def test_udp_bind_returns_handle(self):
        """udp_bind returns non-negative handle."""
        code = '''import "net"
fn main() -> i32 {
    i64 sock = net.udp_bind("127.0.0.1", 19004)
    if sock >= i64(0) {
        net.close(sock)
        return 0
    }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class UdpNewTest(unittest.TestCase):
    """Tests for net.udp_new()"""

    def test_udp_new_returns_handle(self):
        """udp_new returns non-negative handle."""
        code = '''import "net"
fn main() -> i32 {
    i64 sock = net.udp_new()
    if sock >= i64(0) {
        net.close(sock)
        return 0
    }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class UdpSendRecvTest(unittest.TestCase):
    """Tests for UDP send/receive."""

    def test_udp_loopback(self):
        """UDP can send and receive on loopback."""
        code = '''import "net"
fn main() -> i32 {
    i64 receiver = net.udp_bind("127.0.0.1", 19005)
    if receiver < i64(0) { return 1 }
    
    i64 sender = net.udp_new()
    if sender < i64(0) { 
        net.close(receiver)
        return 2 
    }
    
    net.set_timeout(receiver, 1000)
    
    i32 sent = net.udp_send(sender, "127.0.0.1", 19005, "udp test")
    if sent <= 0 {
        net.close(sender)
        net.close(receiver)
        return 3
    }
    
    string data = net.udp_recv(receiver, 1024)
    
    net.close(sender)
    net.close(receiver)
    
    if data == "udp test" { return 0 }
    return 4
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class SetTimeoutTest(unittest.TestCase):
    """Tests for net.set_timeout()"""

    def test_set_timeout_returns_true(self):
        """set_timeout returns true on valid socket."""
        code = '''import "net"
fn main() -> i32 {
    i64 sock = net.udp_new()
    if sock < i64(0) { return 1 }
    
    bool ok = net.set_timeout(sock, 1000)
    net.close(sock)
    
    if ok { return 0 }
    return 2
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_set_timeout_invalid_socket(self):
        """set_timeout returns false on invalid socket."""
        code = '''import "net"
fn main() -> i32 {
    bool ok = net.set_timeout(i64(999), 1000)
    if !ok { return 0 }
    return 1
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


class CloseTest(unittest.TestCase):
    """Tests for net.close()"""

    def test_close_valid_socket(self):
        """close works on valid socket."""
        code = '''import "net"
fn main() -> i32 {
    i64 sock = net.udp_new()
    if sock < i64(0) { return 1 }
    net.close(sock)
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_close_invalid_socket(self):
        """close on invalid socket doesn't crash."""
        code = '''import "net"
fn main() -> i32 {
    net.close(i64(999))
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")

    def test_double_close(self):
        """Double close doesn't crash."""
        code = '''import "net"
fn main() -> i32 {
    i64 sock = net.udp_new()
    net.close(sock)
    net.close(sock)
    return 0
}'''
        rc, out, err = run_vigil(code)
        self.assertEqual(rc, 0, f"stderr: {err}")


if __name__ == "__main__":
    unittest.main()

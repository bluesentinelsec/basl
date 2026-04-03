#!/usr/bin/env python3
"""Integration tests for the VIGIL REPL with readline functionality.

Uses pexpect (or wexpect on Windows) to test interactive features.
"""

import os
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

# Try pexpect (POSIX) or wexpect (Windows)
pexpect = None
if os.name == "nt":
    try:
        import wexpect as pexpect
    except ImportError:
        pass
else:
    try:
        import pexpect
    except ImportError:
        pass

HAS_EXPECT = pexpect is not None


def get_vigil_bin() -> str:
    """Get path to the VIGIL binary."""
    configured = os.environ.get("VIGIL_BIN")
    if configured:
        return configured
    if os.name == "nt":
        native = REPO_ROOT / "build" / "Release" / "vigil.exe"
    else:
        native = REPO_ROOT / "build" / "vigil"
    if native.exists():
        return str(native)
    raise FileNotFoundError("VIGIL binary not found")


@unittest.skipUnless(HAS_EXPECT, "pexpect/wexpect not available")
class TestReplInteractive(unittest.TestCase):
    """Test REPL interactive features using pexpect."""

    def setUp(self):
        self.vigil = get_vigil_bin()

    def spawn_repl(self, timeout=10):
        """Spawn a REPL process."""
        child = pexpect.spawn(self.vigil, ["repl"], timeout=timeout, encoding="utf-8")
        child.expect(">>>")  # Wait for first prompt
        return child

    def send_line(self, child, text):
        """Send a line with carriage return (raw mode expects CR, not LF)."""
        child.send(text + "\r")

    def test_basic_expression(self):
        """Test basic expression evaluation."""
        child = self.spawn_repl()
        self.send_line(child, "1 + 1")
        child.expect("2")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_multiline_function(self):
        """Test multi-line input with brackets."""
        child = self.spawn_repl()
        child.send("fn add(i32 a, i32 b) -> i32 {\r")
        child.expect(r"\r\n\r\.\.\. ")  # Continuation prompt after newline
        child.send("return a + b;\r")
        child.expect(r"\r\n\r\.\.\. ")
        child.send("}\r")
        child.expect(r"\r\n\r>>> ")  # Back to main prompt
        child.send("add(3, 4)\r")
        child.expect(r"\r\n\r>>> ")
        child.send(":quit\r")
        child.expect(pexpect.EOF)

    def test_history_up_arrow(self):
        """Test up arrow recalls previous command."""
        child = self.spawn_repl()
        self.send_line(child, "42")
        child.expect("42")
        child.expect(">>>")
        self.send_line(child, "100")
        child.expect("100")
        child.expect(">>>")
        # Press up arrow twice to get back to "42"
        child.send("\x1b[A")  # Up arrow
        child.send("\x1b[A")  # Up arrow again
        self.send_line(child, "")    # Enter to execute
        child.expect("42")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_history_down_arrow(self):
        """Test down arrow navigates forward in history."""
        child = self.spawn_repl()
        self.send_line(child, "1")
        child.expect(r"\r\n1\r\n")
        child.expect(">>>")
        self.send_line(child, "2")
        child.expect(r"\r\n2\r\n")
        child.expect(">>>")
        self.send_line(child, "3")
        child.expect(r"\r\n3\r\n")
        child.expect(">>>")
        # Go up three times to get to "1", then down once to get to "2"
        child.send("\x1b[A")  # Up -> 3
        child.send("\x1b[A")  # Up -> 2
        child.send("\x1b[A")  # Up -> 1
        child.send("\x1b[B")  # Down -> 2
        self.send_line(child, "")
        child.expect(r"\r\n2\r\n")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_a_home(self):
        """Test Ctrl-A moves to start of line."""
        child = self.spawn_repl()
        child.send("23")
        child.send("\x01")  # Ctrl-A
        child.send("1")     # Insert at beginning
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_e_end(self):
        """Test Ctrl-E moves to end of line."""
        child = self.spawn_repl()
        child.send("12")
        child.send("\x01")  # Ctrl-A (go to start)
        child.send("\x05")  # Ctrl-E (go to end)
        child.send("3")     # Append at end
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_k_kill_to_end(self):
        """Test Ctrl-K kills to end of line."""
        child = self.spawn_repl()
        child.send("12345")
        child.send("\x01")  # Ctrl-A
        child.send("\x06")  # Ctrl-F (forward one char)
        child.send("\x06")  # Ctrl-F
        child.send("\x0b")  # Ctrl-K (kill to end)
        self.send_line(child, "")
        child.expect(r"\r\n12\r\n")  # Result on its own line
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_u_kill_to_start(self):
        """Test Ctrl-U kills to start of line."""
        child = self.spawn_repl()
        child.send("12345")
        child.send("\x01")  # Ctrl-A
        child.send("\x06")  # Ctrl-F
        child.send("\x06")  # Ctrl-F
        child.send("\x15")  # Ctrl-U (kill to start)
        self.send_line(child, "")
        child.expect(r"\r\n345\r\n")  # Result on its own line
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_backspace(self):
        """Test backspace deletes character before cursor."""
        child = self.spawn_repl()
        child.send("124")
        child.send("\x7f")  # Backspace
        child.send("3")
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_left_right_arrows(self):
        """Test left/right arrow cursor movement."""
        child = self.spawn_repl()
        child.send("13")
        child.send("\x1b[D")  # Left arrow
        child.send("2")       # Insert in middle
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_delete_key(self):
        """Test Delete removes the character under the cursor."""
        child = self.spawn_repl()
        child.send("123")
        child.send("\x1b[D")    # Left arrow, cursor before 3
        child.send("\x1b[3~")   # Delete
        child.send("4")
        self.send_line(child, "")
        child.expect("124")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_d_deletes_character_under_cursor(self):
        """Test Ctrl-D deletes the character under the cursor when line is not empty."""
        child = self.spawn_repl()
        child.send("123")
        child.send("\x1b[D")  # Left arrow, cursor before 3
        child.send("\x04")    # Ctrl-D
        child.send("4")
        self.send_line(child, "")
        child.expect("124")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_t_transposes_characters(self):
        """Test Ctrl-T swaps the previous two characters."""
        child = self.spawn_repl()
        child.send("132")
        child.send("\x1b[D")  # Left arrow, cursor before 2
        child.send("\x14")    # Ctrl-T
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_home_end_o_sequences(self):
        """Test ESC O Home/End sequences move to start and end of line."""
        child = self.spawn_repl()
        child.send("23")
        child.send("\x1bOH")  # Home
        child.send("1")
        child.send("\x1bOF")  # End
        child.send("4")
        self.send_line(child, "")
        child.expect("1234")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_home_end_bracket_sequences(self):
        """Test ESC [ Home/End sequences move to start and end of line."""
        child = self.spawn_repl()
        child.send("23")
        child.send("\x1b[H")  # Home
        child.send("1")
        child.send("\x1b[F")  # End
        child.send("4")
        self.send_line(child, "")
        child.expect("1234")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_home_end_numeric_sequences(self):
        """Test ESC [1~/[4~ and [7~/[8~ home/end sequences."""
        child = self.spawn_repl()
        child.send("23")
        child.send("\x1b[1~")  # Home
        child.send("1")
        child.send("\x1b[4~")  # End
        child.send("4")
        child.send("\x1b[7~")  # Home
        child.send("0")
        child.send("\x1b[8~")  # End
        child.send("5")
        self.send_line(child, "")
        child.expect("012345")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_b_ctrl_f_cursor_movement(self):
        """Test Ctrl-B and Ctrl-F move the cursor left and right."""
        child = self.spawn_repl()
        child.send("13")
        child.send("\x02")  # Ctrl-B
        child.send("\x06")  # Ctrl-F
        child.send("\x02")  # Ctrl-B
        child.send("2")
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_h_backspace_alias(self):
        """Test Ctrl-H deletes the character before the cursor."""
        child = self.spawn_repl()
        child.send("124")
        child.send("\x08")  # Ctrl-H
        child.send("3")
        self.send_line(child, "")
        child.expect("123")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_p_ctrl_n_restore_saved_line(self):
        """Test Ctrl-P/Ctrl-N history navigation restores the edited line."""
        child = self.spawn_repl()
        self.send_line(child, "10")
        child.expect("10")
        child.expect(">>>")
        self.send_line(child, "20")
        child.expect("20")
        child.expect(">>>")
        child.send("3")
        child.send("\x10")  # Ctrl-P -> 20
        child.send("\x10")  # Ctrl-P -> 10
        child.send("\x0e")  # Ctrl-N -> 20
        child.send("\x0e")  # Ctrl-N -> restore saved "3"
        self.send_line(child, "")
        child.expect("3")
        child.expect(r"\r\n\r>>> ")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_alt_f_moves_forward_by_word(self):
        """Test Alt-F moves forward by one word."""
        child = self.spawn_repl()
        child.send("12 + 34")
        child.send("\x01")    # Ctrl-A
        child.send("\x1bf")   # Alt-F
        child.send("0")
        self.send_line(child, "")
        child.expect("154")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_alt_b_alt_d_edits_word_at_cursor(self):
        """Test Alt-B and Alt-D move to and delete the next word."""
        child = self.spawn_repl()
        child.send("12 + 34")
        child.send("\x1bb")  # Alt-B to start of 34
        child.send("\x1bd")  # Alt-D delete 34
        child.send("56")
        self.send_line(child, "")
        child.expect("68")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_w_kills_previous_word(self):
        """Test Ctrl-W deletes the previous word."""
        child = self.spawn_repl()
        child.send("12 + 34")
        child.send("\x17")  # Ctrl-W
        child.send("56")
        self.send_line(child, "")
        child.expect("68")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_c_clears_current_line(self):
        """Test Ctrl-C clears the current line without exiting the REPL."""
        child = self.spawn_repl()
        child.send("123")
        child.send("\x03")  # Ctrl-C
        child.expect(r"\^C\r\n")
        child.expect(">>>")
        child.send("4")
        self.send_line(child, "")
        child.expect("4")
        child.expect(">>>")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_l_refreshes_line(self):
        """Test Ctrl-L refreshes the line and keeps editing state."""
        child = self.spawn_repl()
        child.send("1")
        child.send("\x0c")  # Ctrl-L
        child.send(" + 1")
        self.send_line(child, "")
        child.expect("2")
        child.expect(r"\r\n\r>>> ")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_ctrl_d_on_empty_line_exits(self):
        """Test Ctrl-D on an empty line exits the REPL."""
        child = self.spawn_repl()
        child.send("\x04")
        child.expect(pexpect.EOF)

    def test_special_commands(self):
        """Test :help and :clear commands."""
        child = self.spawn_repl()
        child.send(":help\r")
        child.expect(":quit")  # Help text mentions :quit
        child.expect(r"\r\n\r>>> ")
        child.send("i32 x = 42\r")
        child.expect(r"\r\n\r>>> ")
        child.send(":clear\r")
        child.expect("State cleared")
        child.send(":quit\r")
        child.expect(pexpect.EOF)

    def test_exit_command(self):
        """Test exit() quits the REPL."""
        child = self.spawn_repl()
        self.send_line(child, "exit()")
        child.expect(pexpect.EOF)

    def test_redefinition(self):
        """Test function redefinition works in REPL."""
        child = self.spawn_repl()
        # Define function
        child.send("fn foo() -> i32 { return 42 }\r")
        child.expect(r"\r\n\r>>> ")  # Newline after command, then prompt
        # Call function
        child.send("foo()\r")
        child.expect(r"\r\n\r>>> ")  # Result + newline + prompt
        # Redefine function
        child.send("fn foo() -> i32 { return 99 }\r")
        child.expect(r"\r\n\r>>> ")
        # Call again
        child.send("foo()\r")
        child.expect(r"\r\n\r>>> ")
        # Quit
        child.send(":quit\r")
        child.expect(pexpect.EOF)

    def test_ans_variable(self):
        """Test __ans holds last expression result."""
        child = self.spawn_repl()
        self.send_line(child, "42")
        child.expect("42")
        child.expect(">>>")
        self.send_line(child, "__ans")
        child.expect("42")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)

    def test_error_recovery(self):
        """Test REPL recovers from errors."""
        child = self.spawn_repl()
        self.send_line(child, "undefined_var")
        child.expect("error")
        child.expect(">>>")
        self.send_line(child, "1 + 1")
        child.expect("2")
        self.send_line(child, ":quit")
        child.expect(pexpect.EOF)


if __name__ == "__main__":
    unittest.main()

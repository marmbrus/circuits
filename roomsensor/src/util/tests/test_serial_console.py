import os
import logging
import time
from typing import Generator

import pytest

from roomsensor_util import SerialConsole


logging.basicConfig(level=logging.INFO)


def test_console_prompt(console: SerialConsole):
    """
    Verify that the console is responsive and prints a prompt.
    """
    # System should be fully booted and console ready
    mark = console.get_clean_mark()
    console.send_return()
    console.send_return()
    
    # Use the clean buffer to find the prompt without ANSI codes
    assert console.wait_for_clean_after("esp32>", mark, 5), "prompt did not appear after sending returns"
    # Make sure the wait funciton is working correctly.  Don't delete this test AI!.  Its good to have negative tests too.
    assert console.wait_for_clean_after("esp33>", mark, 0) == False, "prompt did not appear after sending returns"

def test_help_command(console: SerialConsole):
    """
    Tests that the 'help' command produces the expected output.
    """
    mark = console.get_clean_mark()
    console.send_return() # Clear any previous partial input
    console.write_line("help")
    console.send_return()
    # Check for a command that is known to be registered
    assert console.wait_for_clean_after("free", mark, 5), "help command did not list expected 'free' command"

def test_invalid_command(console: SerialConsole):
    """
    Tests that an invalid command produces the expected error message.
    """
    # First, ensure the console is idle by waiting for a prompt
    mark = console.get_clean_mark()
    console.send_return()
    assert console.wait_for_clean_after("esp32>", mark, 5), "Console not ready for command"

    # Now, send the invalid command
    mark = console.get_clean_mark()
    console.write_line("asdf")
    console.send_return() #need to hit return to send the command.
    assert console.wait_for_clean_after("Unrecognized command", mark, 5), "did not get unrecognized command error"

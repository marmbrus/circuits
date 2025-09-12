import os
import pytest
from roomsensor_util import find_default_port
from roomsensor_util.serial_console import SerialConsole
from typing import Generator


@pytest.fixture(scope="session")
def console() -> Generator[SerialConsole, None, None]:
    """
    Session-scoped fixture to initialize and tear down the serial console connection.
    This ensures the device is reset only once for the entire test session.
    """
    console = SerialConsole()
    try:
        console.open()
        print("\nPerforming hardware reset for test session...")
        console.reset_normal()
        
        assert console.wait_for("Console initialized", timeout_s=10), "device never initialized"
        assert console.wait_for("Startup sequence complete", timeout_s=30), "device never completed startup"

        yield console
    finally:
        print("\nClosing serial port for test session...")
        console.close()


def pytest_addoption(parser):
    parser.addoption(
        "--serial-port",
        action="store",
        default=None,
        help="Override serial port to use for tests",
    )


def pytest_collection_modifyitems(config, items):
    # If no serial port is available, skip serial tests
    port = config.getoption("--serial-port") or os.environ.get("ROOMSENSOR_SERIAL_PORT") or find_default_port()
    if not port:
        skip_marker = pytest.mark.skip(reason="No serial port available for tests")
        for item in items:
            item.add_marker(skip_marker)


def pytest_exception_interact(node, call, report):
    """
    Hook to dump serial console output on test failure.
    """
    if report.failed:
        try:
            # The 'console' fixture is available on the test function object
            if "console" in node.funcargs:
                console: SerialConsole = node.funcargs["console"]
                console.dump_recent_output()
        except Exception as e:
            print(f"\nError dumping serial console output: {e}")


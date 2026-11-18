import pytest
import os
import sys
from tools.pytest.qemu_menager import QemuMenager,asyncio

global file_list
file_list = []


def pytest_register_test(func):
    func.__test__ = True
    # file_list.append(func.__file__)
    return func

if __name__ == "__main__":
    qemu_thread = None
    logs = os.popen("ps -A").read()

    # ps -A cuts process names to 15 characters:
    # qemu-system-arm (BBB) and qemu-system-aarch64 -> qemu-system-aar (RPi).
    if "qemu-system-arm" in logs or "qemu-system-aar" in logs:
        print("@@@@ Qemu already running @@@@")
    else:
        print("@@@@ Please Start qemu... @@@@")
        sys.exit(-10)

        
    res = pytest.main(file_list)
    sys.exit(res)

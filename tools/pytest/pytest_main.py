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

    if "qemu-system-arm" in logs:
        print("@@@@ Qemu already running @@@@")
    else:
        print("@@@@ Please Start qemu... @@@@")
        sys.exit(-10)

        
    res = pytest.main(file_list)
    sys.exit(res)

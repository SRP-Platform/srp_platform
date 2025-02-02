from tools.pytest.core_lib import UdsClient
import tools.pytest.pytest_main

@tools.pytest.pytest_main.pytest_register_test
def test_uds():
    client = UdsClient("192.168.10.101")
    assert True
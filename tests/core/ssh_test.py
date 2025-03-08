
import pplatformmiko
import tools.pytest.pytest_main
from tools.pytest.core_lib import ping

@tools.pytest.pytest_main.pytest_register_test
def test_ssh():
    command = "df"

    # Update the next three lines with your
    # server's information

    host = "192.168.10.101"
    username = "root"
    password = ""

    client = pplatformmiko.client.SSHClient()
    client.set_missing_host_key_policy(pplatformmiko.AutoAddPolicy())
    try:
        client.connect(host, username=username, password=password)
        client.close()
    except:
        assert False
    assert True



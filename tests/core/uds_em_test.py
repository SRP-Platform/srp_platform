from tools.pytest.core_lib import UdsClient
import tools.pytest.pytest_main
import time
DID_ID = "0004"
DEVICE_IP = "192.168.10.101"





import paramiko

def restart_using_ssh():
    host = "192.168.10.101"
    username = "root"
    password = ""
    client = paramiko.client.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        client.connect(host, username=username, password=password)
        stdin, stdout, stderr = client.exec_command("reboot")
        client.close()
    except:
        assert False
    assert True




@tools.pytest.pytest_main.pytest_register_test
def uds_machine_em_did_read_test():
    client = UdsClient(DEVICE_IP)
    res = client.ReadDiD(DID_ID)
    assert res[0] == 0x62
    assert res[1] == 0x00
    assert res[2] == 0x04
    assert res[3] == 0x03
    for i in range(0, res[3]):
        assert res[6 + 3*i] == 0x02
    res = client.WriteDiD("0003", "2e6c")
    # assert res
    assert res[0] == 0x6e
    time.sleep(2)
    res = client.ReadDiD(DID_ID)
    assert res[0] == 0x62
    assert res[1] == 0x00
    assert res[2] == 0x04
    assert res[3] == 0x03
    assert res[12] == 0x04
    restart_using_ssh()
    time.sleep(20)

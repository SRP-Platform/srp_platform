from tools.pytest.core_lib import UdsClient
import tools.pytest.pytest_main

@tools.pytest.pytest_main.pytest_register_test
def uds_machine_state_did_read_test():
    client = UdsClient("192.168.10.101")
    res = client.ReadDiD("0003")
    assert res[0] == 0x62
    
@tools.pytest.pytest_main.pytest_register_test
def uds_machine_state_did_write_test():
    client = UdsClient("192.168.10.101")
    res = client.ReadDiD("0003")
    assert res[0] == 0x62
    res = client.WriteDiD("0003","b6b3")
    assert res[0] == 0x6E
    res = client.ReadDiD("0003")
    assert res[3] == 0xB6
    assert res[4] == 0xB3
    res = client.WriteDiD("0003","8b72")
    assert res[0] == 0x6E

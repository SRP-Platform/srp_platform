from tools.pytest.core_lib import UdsClient
import tools.pytest.pytest_main

DID_ID = "0004"
DEVICE_IP = "192.168.10.101"

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

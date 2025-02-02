import tools.pytest.pytest_main
from tools.pytest.core_lib import ping

@tools.pytest.pytest_main.pytest_register_test
def test_ecu_is_online():
    max_attempts  = 10
    attempts = 0
    sleep_time = 5
    test_fla = False
    
    while(test_fla!=True):
        test_fla = ping("192.168.10.101")
        attempts+=1
        assert max_attempts > attempts
    
    assert test_fla

import time
from tools.pytest.core_lib import ping

if __name__ == "__main__":
    max_attempts  = 100
    attempts = 0
    sleep_time = 0
    test_fla = False
    
    while(test_fla!=True):
        test_fla = ping("192.168.10.101")
        attempts+=1
        assert max_attempts > attempts
        if not test_fla:
            time.sleep(sleep_time)
    time.sleep(5)
    assert test_fla
import platform    # For getting the operating system name
import subprocess  # For executing a shell command
from doipclient import DoIPClient


def ping(host):
    """
    Returns True if host (str) responds to a ping request.
    Remember that a host may not respond to a ping (ICMP) request even if the host name is valid.
    """

    # Option for the number of packets as a function of
    pplatformm = '-n' if platform.system().lower()=='windows' else '-c'

    # Building the command. Ex: "ping -c 1 google.com"
    command = ['ping', pplatformm, '1', host]

    return subprocess.call(command) == 0

class UdsClient:
    def __init__(self,ip,):
        self.client = DoIPClient(ip,0x0A)
    def Rx(self):
        res = None
        while True:
            res = bytearray(self.client.receive_diagnostic())
            if(res[0] == 0x7f and res[2] == 0x78):
                continue
            else:
                break
        return res
    def ReadDiD(self,id):
        self.client.send_diagnostic(bytearray.fromhex("22"+id))
        return self.Rx()
    
    def WriteDiD(self,id,val):
        self.client.send_diagnostic(bytearray.fromhex("2E"+id+val))
        return self.Rx()
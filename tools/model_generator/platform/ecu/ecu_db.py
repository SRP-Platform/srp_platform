from __future__ import annotations
from tools.model_generator.platform.common.singleton_lib import SingletonMeta

class CommonItem:
    def __init__(self, use:str):
        self.use = use

class NetworkItem:
    def __init__(self, ip_address, network_mask):
        self.ip_address = ip_address
        self.network_mask = network_mask

class SomeipSdItem:
    def __init__(self, multicast_group, multicast_port):
        self.multicast_group = multicast_group
        self.multicast_port = multicast_port

class SomeipEndpoint(CommonItem):
    def __init__(self, use, sd, port):
        super().__init__(use)
        self.sd = sd
        self.port = port

class SomeipEndpointAlias:
    def __init__(self, alias):
        self.alias = alias

class DoipItem:
    def __init__(self, announcement_ip, announcement_port, interface):
        self.announcement_ip = announcement_ip
        self.announcement_port = announcement_port
        self.interface = interface

class Logging(CommonItem):
    def __init__(self, use, tx_port:int, ecu_id:str):
        super().__init__(use)
        self.tx_port = tx_port
        self.ecu_id = ecu_id

class Diagnostic(CommonItem):
    def __init__(self, use, protocol:str, logical_address:int):
        super().__init__(use)
        self.protocol = protocol
        self.logical_address = logical_address

class Ecu:
    def __init__(self, logging:Logging, diagnostic:Diagnostic):
        self.diagnostic = diagnostic
        self.logging = logging
        self.transport_layer = {}
        self.someip_endpoints = {}


class EcuDb(metaclass=SingletonMeta):
    def __init__(self) -> None:
        self.data_structure: dict[str,Ecu] = {}
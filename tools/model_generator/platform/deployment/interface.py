from __future__ import annotations

class InterfaceCommonConfig:
    def __init__(self,ServiceId:int, model_name:str):
        self.ServiceId = ServiceId
        self.ModelName = model_name

class InterfaceEndpointConfig:
    def __init__(self, name:str, EndpointId:int):
        self.name = name
        self.EndpointId = EndpointId

class InterfaceDepl:
    def __init__(self,
                 name:str,
                 interface_name:str,
                 common_conf:InterfaceCommonConfig,
                 EndpointList:dict[str,InterfaceEndpointConfig]):
        self.name = name
        self.interface_name = interface_name
        self.common_conf = common_conf
        self.EndpointList = EndpointList
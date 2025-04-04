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

class DiagJobDepl:
    def __init__(self, service_type, subs_service_id, active_sesion, ecu_mode):
        self.service_type = service_type
        self.subs_service_id = subs_service_id
        self.active_sesion = active_sesion
        self.ecu_mode = ecu_mode
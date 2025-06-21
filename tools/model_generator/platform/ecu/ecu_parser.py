from __future__ import annotations
from tools.model_generator.platform.ecu.ecu_db import *


class EcuParser:
    def ParseJson(json):
        package = "/"
        if "package" in json:
            package = "/"+(json["package"]+".").replace(".","/")
        for key, item in json["ecu"].items():
            temp_ecu = Ecu(EcuParser._ParseLogging(item), EcuParser._ParseDiagnostic(item))
            temp_ecu.someip_endpoints = EcuParser._ParseSomeipEndpoints(item)
            temp_ecu.transport_layer = EcuParser._ParseTransportLayer(item)
            EcuDb().data_structure[package+key] = temp_ecu

    def _ParseLogging(json) -> Logging:
        temp = json["logging"]
        return Logging(temp["use"],temp["tx_port"],temp["ecu_id"])

    def _ParseDiagnostic(json) -> Diagnostic:
        temp = json["diagnostic"]
        return Diagnostic(temp["use"],temp["protocol"],temp["logical_address"])

    def _ParseSomeipEndpoints(json):
        res = {}
        for key,item in json["someip_endpoints"].items():
            if "alias" in item:
                res["someip_endpoints/"+key] = SomeipEndpointAlias(item["alias"])
            else:
                res["someip_endpoints/"+key] = SomeipEndpoint(item["use"],item["sd"],item["port"])
        return res
    
    def _ParseTransportLayer(json):
        res = {}
        if "network" in json["transport_layer"]:
            for key, item in json["transport_layer"]["network"].items():
                res["transport_layer/network/"+key] = NetworkItem(item["ip_address"],item["network_mask"])

        if "someip_sd" in json["transport_layer"]:
            for key, item in json["transport_layer"]["someip_sd"].items():
                res["transport_layer/someip_sd/"+key] = SomeipSdItem(item["multicast_group"],item["multicast_port"])

        if "doip" in json["transport_layer"]:
            res["transport_layer/doip"] = DoipItem(json["transport_layer"]["doip"]["announcement_ip"],
                                                   json["transport_layer"]["doip"]["announcement_port"],
                                                   json["transport_layer"]["doip"]["interface"])
        return res
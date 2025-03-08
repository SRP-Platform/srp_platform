from __future__ import annotations
import logging

from tools.model_generator.platform.interface.interface import Interface,Attribute,Broadcast,Method
from tools.model_generator.platform.interface.interface_db import InterfaceDb

logger = logging.getLogger(__name__)

class InterfaceParser:
    def _MethodParser(json_obj)->dict[str,Method]:
        res:dict[str,Method] = {}
        for key, obj in json_obj.items():
            in_t = "void" 
            out_t = "void"
            if "in" in obj:
                in_t = obj["in"]
            if "out" in obj:
                out_t = obj["out"]
            if key in res:
                raise f"Method with name: {key} already exist"
            res[key] = Method(key,in_t,out_t)
        return res

    def _BroadcastParser(json_obj) -> dict[str,Broadcast]:
        res:dict[str,Broadcast] = {}
        for key, obj in json_obj.items():
            if key in res:
                raise f"Boradcast with name: {key} already exist"
            res[key] = Broadcast(key,obj["out"])    
        return res
    def _AttributesParser(josn_obj) -> dict[str,Attribute]:
        res:dict[str,Attribute] = {}
        for key, obj in josn_obj.items():
            readonly = False
            if "readonly" in obj:
                readonly = obj["readonly"]
            if key in res:
                raise f"Attribute with name: {key} already exist"
            res[key] = Attribute(key,obj["out"],readonly)
        return res

    def Parser(json_obj):
        package = ""
        major_version = 0
        minor_version = 0
        if "package" in json_obj:
            package = json_obj["package"]
        
        interface_obj = None
        if "interface" not in json_obj:
            logger.critical("Interface Object not exist in file")
        interface_obj = json_obj["interface"]
        key = ""
        try:
            key = list(interface_obj.keys())[0]
        except:
            logger.critical("NO interface in def file")
        interface_obj = interface_obj[key]

        methods:dict[str,Method] = {}
        broadcasts:dict[str,Broadcast] = {}
        attributes:dict[str,Attribute] = {}

        if "methods" in interface_obj:
            methods = InterfaceParser._MethodParser(interface_obj["methods"])
        if "broadcasts" in interface_obj:
            broadcasts = InterfaceParser._BroadcastParser(interface_obj["broadcasts"])
        if "attributes" in interface_obj:
            attributes = InterfaceParser._AttributesParser(interface_obj["attributes"])
        if "major_version" in interface_obj:
            major_version = interface_obj["major_version"]
        if "minor_version" in interface_obj:
            minor_version = interface_obj["minor_version"]

        temp_i = Interface(key,"/"+package.replace(".","/"),major_version,minor_version,methods,broadcasts,attributes)
        InterfaceDb().AddInterface(temp_i)


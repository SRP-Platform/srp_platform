from __future__ import annotations
from tools.model_generator.ara.someip.lib.someip_db import SomeIpDb
from tools.model_generator.ara.someip.lib.service import *
from tools.model_generator.ara.common.data_structure_db import DataStructureDB

import json

from tools.model_generator.ara.mavlink.mavlink_dialect import CheckElementKeys
from tools.model_generator.ara.mavlink.mavlink_errors import MavlinkModelError

class SomeIpParser:
    def LoadJson(path:str):
        with open(path,"r") as json_file:
            json_obj = json.load(json_file)
            SomeIpParser.ParseJson(json_obj)
            
    def ParseJson(json_obj, db_ = SomeIpDb()):
            if "someip" in json_obj:
                SomeIpParser.LoadSomeipService(json_obj,db_)
                

    def LoadSomeipService(json_obj,db_:SomeIpDb):
        name = ""
        if "package" in json_obj:
            name = json_obj["package"]
        assert "someip" in json_obj
        temp_list = list(json_obj["someip"].keys())
        if len(name) > 0:
            name += ("."+temp_list[0])
        else:
            name = temp_list[0]
        temp_service = Service()
        temp_service.name = name
        temp_service.id = json_obj["someip"][temp_list[0]]["service_id"]
        temp_service.major = json_obj["someip"][temp_list[0]]["major_version"]
        temp_service.minor = json_obj["someip"][temp_list[0]]["minor_version"]
        if "methods" in json_obj["someip"][temp_list[0]]:
            temp_service.methods.extend(SomeIpParser.ExportMethodsList(json_obj["package"],json_obj["someip"][temp_list[0]]["methods"],db_,name))
        if "events" in json_obj["someip"][temp_list[0]]:
            temp_service.events.extend(SomeIpParser.ExportEventsList(json_obj["package"],json_obj["someip"][temp_list[0]]["events"],db_,name))
        SomeIpDb().service_list[name] = temp_service
            
        
    def Where(package:str, service:str, element:str) -> str:
        """Nazwa elementu do komunikatów błędów: pakiet.Usługa.Element."""
        parts = [x for x in (service if service.startswith(package + ".") else (package + "." + service if service else package), element) if x]
        return ".".join(parts)

    def Variable(name:str, package:str, v_type:str, where:str):
        """CreateVariable z czytelnym błędem generatora zamiast tracebacku."""
        try:
            return DataStructureDB().CreateVariable(name, package, v_type)
        except AssertionError as e:
            raise MavlinkModelError(f"{where}: nieznany typ '{v_type}' ({e})")

    def ExportMethodsList(package:str,method_json_list,db_:SomeIpDb,service:str = "") -> list[Method]:
        res: list[Method] = []
        for key, json_object in method_json_list.items():
            res.append(SomeIpParser.ExportMethod(key,package,json_object,db_,service))
        return res

    def ExportEventsList(package:str,method_json_list,db_:SomeIpDb,service:str = "") -> list[Event]:
        res: list[Event] = []
        for key, json_object in method_json_list.items():
            res.append(SomeIpParser.ExportEvent(key,package,json_object,db_,service))
        return res

    def ExportMethod(name:str,package:str,json_object,db_:SomeIpDb,service:str = "") -> Method:
        where = SomeIpParser.Where(package, service, name)
        CheckElementKeys(json_object.keys(), where)
        input_parm = SomeIpParser.Variable("in_parm", package, json_object["data_structure"]["in"]["type"], where)
        output_parm = SomeIpParser.Variable("out", package, json_object["data_structure"]["out"]["type"], where)
        method = Method(name,json_object["id"],input_parm,output_parm)
        method.mavlink = json_object.get("mavlink")
        method.desc = json_object.get("desc", "")
        return method
    
    def ExportEvent(name:str,package:str,json_object,db_:SomeIpDb,service:str = "") -> Event:
        where = SomeIpParser.Where(package, service, name)
        CheckElementKeys(json_object.keys(), where)
        output_parm = SomeIpParser.Variable("out", package, json_object["data_structure"]["out"]["type"], where)
        event = Event(name,json_object["id"],output_parm)
        event.mavlink = json_object.get("mavlink")
        event.desc = json_object.get("desc", "")
        return event
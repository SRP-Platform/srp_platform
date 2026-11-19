from __future__ import annotations
from tools.model_generator.ara.someip.lib.someip_db import SomeIpDb
from tools.model_generator.ara.someip.lib.service import *
from tools.model_generator.ara.common.data_structure_db import DataStructureDB
from tools.model_generator.ara.common.model_error import ModelError

import json

# Keys allowed in a method/event of service.json; any other key is an error (a misspelled
# "mavlink" would silently exclude the element from MAVLink).
ELEMENT_KEYS = ("id", "data_structure", "desc", "mavlink")

def Require(json_object, where:str, *keys:str):
    """json_object[k1][k2]...; a missing key or a non-object level is a ModelError naming `where`."""
    obj = json_object
    for i, key in enumerate(keys):
        if not isinstance(obj, dict) or key not in obj:
            raise ModelError(f"{where}: missing '{'.'.join(keys[:i + 1])}'")
        obj = obj[key]
    return obj

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
        if not isinstance(json_obj["someip"], dict) or not json_obj["someip"]:
            raise ModelError("'someip' must be an object with the service name as the key")
        temp_list = list(json_obj["someip"].keys())
        if len(name) > 0:
            name += ("."+temp_list[0])
        else:
            name = temp_list[0]
        temp_service = Service()
        temp_service.name = name
        temp_service.id = Require(json_obj["someip"], name, temp_list[0], "service_id")
        temp_service.major = Require(json_obj["someip"], name, temp_list[0], "major_version")
        temp_service.minor = Require(json_obj["someip"], name, temp_list[0], "minor_version")
        if "methods" in json_obj["someip"][temp_list[0]]:
            temp_service.methods.extend(SomeIpParser.ExportMethodsList(json_obj["package"],json_obj["someip"][temp_list[0]]["methods"],db_,name))
        if "events" in json_obj["someip"][temp_list[0]]:
            temp_service.events.extend(SomeIpParser.ExportEventsList(json_obj["package"],json_obj["someip"][temp_list[0]]["events"],db_,name))
        SomeIpDb().service_list[name] = temp_service
            
        
    def CheckElementKeys(json_object, name:str, service:str):
        if not isinstance(json_object, dict):
            raise ModelError(f"{service}.{name}: must be an object")
        for key in json_object:
            if key not in ELEMENT_KEYS:
                raise ModelError(f"{service}.{name}: unknown key '{key}' (allowed: {', '.join(ELEMENT_KEYS)})")

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
        SomeIpParser.CheckElementKeys(json_object, name, service)
        where = f"{service}.{name}"
        input_parm = DataStructureDB().CreateVariable("in_parm",package,Require(json_object,where,"data_structure","in","type"))
        output_parm = DataStructureDB().CreateVariable("out",package,Require(json_object,where,"data_structure","out","type"))
        method = Method(name,Require(json_object,where,"id"),input_parm,output_parm)
        method.mavlink = json_object.get("mavlink")
        method.desc = json_object.get("desc", "")
        return method
    
    def ExportEvent(name:str,package:str,json_object,db_:SomeIpDb,service:str = "") -> Event:
        SomeIpParser.CheckElementKeys(json_object, name, service)
        where = f"{service}.{name}"
        output_parm = DataStructureDB().CreateVariable("out",package,Require(json_object,where,"data_structure","out","type"))
        event = Event(name,Require(json_object,where,"id"),output_parm)
        event.mavlink = json_object.get("mavlink")
        event.desc = json_object.get("desc", "")
        return event
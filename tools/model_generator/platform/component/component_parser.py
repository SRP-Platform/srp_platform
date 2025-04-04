from __future__ import annotations

import logging

from tools.model_generator.platform.component.component import *
from tools.model_generator.platform.component.component_db import ComponentDb

logger = logging.getLogger(__name__)

class ComponentParser:
    def _NameResolver(name:str):
        src_name = name.split(" as ")[0]
        desc_name = name.split(" as ")[1]
        return src_name, desc_name
    def _ExtractDiag(json_obj, name, model):
        return DiagItem(name,"/"+model.replace(".","/"),json_obj["instanceID"])
    def _ExtractIpc(json_obj, name, model):
        instance = 0
        if "instanceID" in json_obj:
            instance = json_obj["instanceID"]
        return IpcItem(name,"/"+model.replace(".","/"),"/"+json_obj["using"].replace(".","/"), instance)

    def _ProvideParser(json_obj,pkg:str):
        list = {}
        for key, obj in json_obj.items():
            model_name, item_name = ComponentParser._NameResolver(key)
            if obj["on"] in ["IPC", "ipc"]:
                item = ComponentParser._ExtractIpc(obj, pkg+"/"+item_name, model_name)
                if item.instance == 0:
                    raise logger.critical("Missing InstanceId in provide")
                    
                list[pkg+"/"+item_name] = item
            elif obj["on"].upper() in ["DIAG"]:
                item = ComponentParser._ExtractDiag(obj, pkg+"/"+item_name, model_name)
                list[item.name] = item
        return list
                


    def _RequireParser(json_obj,pkg:str):
        list = {}
        for key, obj in json_obj.items():
            model_name, item_name = ComponentParser._NameResolver(key)
            if obj["on"].upper() in ["IPC"]:
                item = ComponentParser._ExtractIpc(obj, pkg+"/"+item_name, model_name)
                if item.instance == 0:
                    item.instance == 0xFFFF
                list[pkg+"/"+item_name] = item
        return list
                
    def Parser(json_obj):
        package = json_obj["package"]
        json_obj = json_obj["component"]
        for key, obj in json_obj.items():
            parms = ""
            if "parms" in json_obj:
                parms = json_obj["parms"]
            logger = ComponentParser._LoggerParser(obj["logger"])
            comp = Component("/"+package.replace(".","/")+"/"+key,logger,obj["functional_groups"],obj["reporting_mode"],parms)
            comp.provide = ComponentParser._ProvideParser(obj["provide"], "/"+package.replace(".","/")+"/"+key+"_SWRoot")
            comp.required = ComponentParser._RequireParser(obj["require"], "/"+package.replace(".","/")+"/"+key+"_SWRoot")
            ComponentDb().list["/"+package.replace(".","/")+"/"+key] = comp


    def _LoggerParser(json_object) -> LogerConfig:
        logger = LogerConfig(json_object["app_id"],json_object["app_des"],json_object["log_level"],json_object["log_mode"])
        if "ctx" in json_object:
            for item in json_object["ctx"]:
                des = ""
                if "ctx_des" in item:
                    des = item["ctx_des"]
                logger.ctx_list.append(LoggerCtx(item["ctx_id"],item["log_level"],des))
        return logger
    
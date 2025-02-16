from __future__ import annotations
import logging

from tools.model_generator.ara.deployment.interface import InterfaceCommonConfig,InterfaceDepl,InterfaceEndpointConfig
from tools.model_generator.ara.deployment.deployment_db import DeploymentDb

logger = logging.getLogger(__name__)

class DeploymentParser:
    def _ResolvePkg(name:str, pkg):
        src_name = name.split(" as ")[0]
        desc_name = name.split(" as ")[1]
        if "." not in src_name:
            src_name = pkg+"."+src_name
        return src_name, desc_name

    def Parser(json_obj):
        package = json_obj["package"]
        json_obj = json_obj["deployment"]
        if "interface" in json_obj:
            DeploymentParser._InterfaceParser(json_obj["interface"],package)

    def _BasicEndpointParser(json_obj, container):
        for key,obj in json_obj.items():
            container[key] = InterfaceEndpointConfig(key,obj["EndpointId"])

    def _AttributesParser(json_obj,container):
        for key,obj in json_obj.items():
            container[key] = InterfaceEndpointConfig(key,obj["EndpointId"])
            if "GetterId" in obj:
                container[key+"/get"] = InterfaceEndpointConfig(key+"/get",obj["GetterId"])
            else:
                container[key+"/get"] = InterfaceEndpointConfig(key+"/get",0)
            if "SetterId" in obj:
                container[key+"/set"] = InterfaceEndpointConfig(key+"/set",obj["SetterId"])

    def _InterfaceParser(json_obj, pkg_name:str):
        for key, obj in json_obj.items():
            src_name, desc_name = DeploymentParser._ResolvePkg(key,pkg_name)
            general_config = InterfaceCommonConfig(obj["ServiceId"], "/"+src_name.replace(".","/"))
            endpoint:dict[str:InterfaceEndpointConfig] = {}

            if "methods" in obj:
                DeploymentParser._BasicEndpointParser(obj["methods"],endpoint)
            if "broadcasts" in obj:
                DeploymentParser._BasicEndpointParser(obj["broadcasts"],endpoint)
            if "attributes" in obj:
                DeploymentParser._AttributesParser(obj["attributes"],endpoint)
            model = InterfaceDepl(desc_name,"/"+src_name.replace(".","/"),general_config,endpoint)
            DeploymentDb().InsterModel("/"+pkg_name.replace(".","/")+"/"+desc_name, model)
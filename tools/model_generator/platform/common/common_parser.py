from __future__ import annotations
import json
import os
import re 
from tools.model_generator.platform.common.data_structure_parser import DataStructureParser
from tools.model_generator.platform.diag.diag_parser import DiagParser
from tools.model_generator.platform.interface.interface_parser import InterfaceParser
from tools.model_generator.platform.deployment.deployment_parser import DeploymentParser
from tools.model_generator.platform.component.component_parser import ComponentParser
class CommonParser:
    def PathR(path) -> str:
        if "@" in path:
            key = str(re.search(r'@\(([^)]+)\)', path).group(1))
            print(key)
            if key in os.environ:
                print("1")
                path = path.replace("@("+key+")",os.environ[key])
            else:
                print("2")
                path = path.replace(f"@({key})/","")
            print(path)
        return  path
    
    def LoadJson(path:str):
        with open(path,"r") as json_file:
            json_object = json.load(json_file)
            
            if "include" in json_object:
                for path_ in json_object["include"]:
                    
                    CommonParser.LoadJson(CommonParser.PathR(path_))
                    
            if "data_structure" in json_object:
                DataStructureParser.LoadDataStructure(json_object)
            if "interface" in json_object:
                InterfaceParser.Parser(json_object)
            if "deployment" in json_object:
                DeploymentParser.Parser(json_object)
            if "component" in json_object:
                ComponentParser.Parser(json_object)   
            
            # if "diag" in json_object:
            #     DiagParser.ParseJson(json_object)
from __future__ import annotations
import json
from tools.model_generator.ara.common.data_structure_parser import DataStructureParser
from tools.model_generator.ara.someip.lib.someip_parser import SomeIpParser
from tools.model_generator.ara.app.adaptive_application_parser import AdaptiveApplicationParser
from tools.model_generator.ara.diag.diag_parser import DiagParser
class CommonParser:
    def LoadJson(path:str):
        with open(path,"r") as json_file:
            json_object = json.load(json_file)
            
            if "include" in json_object:
                for path_ in json_object["include"]:
                    CommonParser.LoadJson(path_)
                    
            if "data_structure" in json_object:
                DataStructureParser.LoadDataStructure(json_object)
                
            if "someip" in json_object:
                SomeIpParser.ParseJson(json_object)
                
            if "adaptive_application" in json_object:
                AdaptiveApplicationParser.ParseJson(json_object)
            
            if "diag" in json_object:
                DiagParser.ParseJson(json_object)
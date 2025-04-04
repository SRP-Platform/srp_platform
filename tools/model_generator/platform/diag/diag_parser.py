from __future__ import annotations
from tools.model_generator.platform.diag.diag_db import DiagDb
from tools.model_generator.platform.diag.diag_jobs import *
from tools.model_generator.platform.diag.diag_dtc import diag_dtc
class DiagParser:
    def ParseJson(json_object):
        package = json_object["package"]
        json_object = json_object["diagnostic"]
        
        if "ServiceType" in json_object:
            DiagParser.ParseGlobalJob(json_object["ServiceType"],package)

    def ParseGlobalJob(json_object, package:str):
        for key, obj in json_object.items():
            name = "/"+package.replace(".","/")+"/"+key
            uds_id = []
            if "UdsId" in obj:
                uds_id = obj["UdsId"]
            else:
                uds_id = [obj["ServiceId"]]
            model = ServiceType(obj["ServiceId"],obj["SubServiceMaxSize"],obj["SubServiceMinSize"], uds_id)
            if "SkipSubServiceId" in obj:
                model.skip_sub_service_id = obj["SkipSubServiceId"]
            DiagDb().items[name] = model

from __future__ import annotations
from tools.model_generator.platform.common.singleton_lib import SingletonMeta
from tools.model_generator.platform.diag.diag_jobs import *
from tools.model_generator.platform.diag.diag_dtc import diag_dtc
class DiagDb(metaclass=SingletonMeta):
    def __init__(self) -> None:
        self.global_jobs: dict[str,diag_job_global] = {}
        self.jobs: dict[str,diag_job] = {}
        self.dtcs: dict[str,diag_dtc] = {}

    def FindDtc(self,key:str,package:str) -> diag_dtc:
        if key in self.dtcs:
            return self.dtcs[key]
        if (package+"."+key) in self.dtcs:
            return self.dtcs[(package+"."+key)]
        raise Exception("DTC: "+key+" or "+(package+"."+key)+" not found")

    def FindJob(self,key,package) -> diag_job:
        if key in self.jobs:
            return self.jobs[key]
        if (package+"."+key) in self.jobs:
            return self.jobs[(package+"."+key)]
        raise Exception("DTC: "+key+" or "+(package+"."+key)+" not found")
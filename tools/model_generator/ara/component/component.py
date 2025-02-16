from __future__ import annotations
import hashlib

class LoggerCtx:
    def __init__(self,ctx_id:str,log_level:str,ctx_des:str) -> None:
        self.ctx_id = ctx_id
        self.log_level = log_level
        self.ctx_des = ctx_des
    def GetCtxId(self) -> str:
        temp_app_id = ""+self.ctx_id
        if len(temp_app_id) < 4:
            for i in range(0,4-len(temp_app_id)):
                temp_app_id+="-"
        return temp_app_id
    def GetLogLevel(self) -> str:
        if "kVerbose" in self.log_level:
            return "log::LogLevel::kVerbose"
        if "kDebug" in self.log_level:
            return "log::LogLevel::kDebug"
        if "kInfo" in self.log_level:
            return "log::LogLevel::kInfo"
        if "kWarn" in self.log_level:
            return "log::LogLevel::kWarn"
        if "kError" in self.log_level:
            return "log::LogLevel::kError"
        
class LogerConfig:
    def __init__(self,app_id:str,app_des:str,log_level:str,log_mode:str) -> None:
        self.app_id = app_id
        self.app_des = app_des
        self.log_level = log_level
        self.log_mode = log_mode
        self.ctx_list: list[LoggerCtx] = []
        
    def GetAppId(self) -> str:
        temp_app_id = ""+self.app_id
        if len(temp_app_id) < 4:
            for i in range(0,4-len(temp_app_id)):
                temp_app_id+="-"
        return temp_app_id
    def GetLogLevel(self) -> str:
        if "kVerbose" in self.log_level:
            return "log::LogLevel::kVerbose"
        if "kDebug" in self.log_level:
            return "log::LogLevel::kDebug"
        if "kInfo" in self.log_level:
            return "log::LogLevel::kInfo"
        if "kWarn" in self.log_level:
            return "log::LogLevel::kWarn"
        if "kError" in self.log_level:
            return "log::LogLevel::kError"
    def GetLogMode(self) -> str:
        res = ""
        if  "kRemote" in self.log_mode:
            if(len(res)==0):
                res =  "log::LogMode::kRemote"
            else:
                res+= " | log::LogMode::kRemote"
        if  "kFile" in self.log_mode:
            if(len(res)==0):
                res =  "log::LogMode::kFile"
            else:
                res+= " | log::LogMode::kFile"
        if  "kConsole" in self.log_mode:
            if(len(res)==0):
                res =  "log::LogMode::kConsole"
            else:
                res+= "|log::LogMode::kConsole"
        if(len(res)==0):
            raise Exception("kRemote , kFile or kConsole not found in log mode")
        return res

class IpcItem:
    def __init__(self, name:str, model:str,depl:str,instance:int):
        self.name = name
        self.model = model
        self.depl = depl
        self.instance = instance

class Component:
    def __init__(self,name:str,logger:LogerConfig, functional_groups:list[str],reporting_mode:str, parms:str):
        self.name = name
        self.id = int(hashlib.shake_128(name.encode('utf-8')).hexdigest(4), 32)
        self.logger = logger
        self.parms = parms
        self.functional_groups = functional_groups
        self.reporting_mode = reporting_mode
        self.provide = {}
        self.required = {}
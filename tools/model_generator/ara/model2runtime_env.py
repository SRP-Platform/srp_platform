from __future__ import annotations
import sys
import os
import jsonpickle
import copy
from datetime import date
from tools.model_generator.ara.common.common_parser import CommonParser
from tools.model_generator.ara.interface.interface_db import InterfaceDb, Interface
from tools.model_generator.ara.deployment.interface import *
from tools.model_generator.ara.component.component_db import ComponentDb, Component
from tools.model_generator.ara.component.component import IpcItem
from tools.model_generator.ara.common.data_structure import Structure
from tools.model_generator.ara.common.data_structure_extractor import DataStructureExtractor
from tools.model_generator.ara.common.data_structure_db import DataStructureDB

OUTPUT_FILE_START = f"""/**
 * @file initialization.cc
 * ARA Env Generator by Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date """+str(date.today())+""""
 * 
 * @copyright Copyright (c) 2024
 * 
 * !! Automatically generated file please do not change anything !!
 *
 */
#include "ara/com/initialization.h"

#include <stop_token>

#include "ara/core/model/com_model_item.h"
#include "ara/core/model/diag_model_item.h"
#include "ara/core/model_db.h"
#include "ara/log/logging_menager.h"

namespace ara {
namespace core {
namespace {
    std::stop_source ara_stop_source_{};
}
Result<void> InitializeDb(ModelDataBase& db_) noexcept;
Result<void> InitializeLogger() noexcept;
void InitCom() noexcept;
Result<void> Initialize() noexcept {
  std::ignore = InitializeLogger();
  const auto ara_logger_ = log::LoggingMenager::GetInstance()->CreateLogger("ara-");
  ara_logger_.LogInfo() << "Logger initialized";
  {
    auto& db_ = ModelDataBase::GetInstance();
    ara_logger_.LogInfo() << "Instances of the model database have been created";
    std::ignore = InitializeDb(db_);
    ara_logger_.LogInfo() << "Models added";
  }
  
  ara_logger_.LogInfo() << "ARA environment has been initialized";
  InitCom();
  ara_logger_.LogInfo() << "ARA::COM environment has been initialized";
  return {};
}

Result<void> Deinitialize() noexcept {
  ara_stop_source_.request_stop();
  const auto ara_logger_ = log::LoggingMenager::GetInstance()->CreateLogger("ara-");
  ara_logger_.LogInfo() << "ARA environment has been deinitialized";
  
  return {}; 
}

"""

OUTPUT_FILE_END = """}  // namespace core
}  // namespace ara
"""


def CreateLogerInit(model:Component)->str:
        res = """
Result<void> InitializeLogger() noexcept {
    log::LoggingMenager::Create(\""""+model.logger.GetAppId()+"""\","""+model.logger.GetLogMode()+""","""+model.logger.GetLogLevel()+""");
"""
        have_ara = False
        for ctx in model.logger.ctx_list:
            if ctx.GetCtxId() == "ara-":
                have_ara = True
            res+="""    std::ignore = log::LoggingMenager::GetInstance()->CreateLogger(\""""+ctx.GetCtxId()+"""\", \""""+ctx.ctx_des+"""\", """+ctx.GetLogLevel()+""");
"""
        if not have_ara:
            res +="""
    std::ignore = log::LoggingMenager::GetInstance()->CreateLogger("ara-", "Default ctx for ara", log::LogLevel::kInfo);
"""          
        
        res +="""
    return {};
}
"""
        return res

def CreateInterfaceDepl(key, item: InterfaceDepl,sec) -> str:
    res = "    {\n"
    res += f"""      const ara::com::model::ServiceModel service_model{"{"}{hex(item.common_conf.ServiceId).upper().replace("X","x")}U,{hex(sec.instance).upper().replace("X","x")}U{"}"};
"""
    res += """      const ara::com::InstanceIdentifierContainer container{service_model,{\n"""
    for k,i in item.EndpointList.items():
         res+=f"""          {"{"}\"{k}\",ara::com::model::EndpointModel{"{"}{hex(i.EndpointId).upper().replace("X","x")}{"}},"}\n"""
    if res[-2:] == ',\n':
         res = res[:-2]+"\n"
    res += "      }};\n"
    res += f"""      std::ignore = db_.AddNewItem("{key}",ara::core::model::ModelCom(container));\n"""
    res +="    }\n"
    return res
def CreatInitCom(model: Component) -> str:
    #  ;
     return """
void InitCom() noexcept {
    ara::com::Initialize("""+hex(model.id).upper().replace("X","x")+""");
}
"""
def CreateDbInit(model:Component,items)->str:
    res = """
Result<void> InitializeDb(ModelDataBase& db_) noexcept {

"""
    for key, itemd in items.items():
        item = itemd[0]
        depl = itemd[1]
        print(depl)
        if type(depl) == InterfaceDepl:
             res+=CreateInterfaceDepl(key,depl,item)+"\n"
    res += """    return {};
}
"""
    return res

if __name__ == "__main__":
    out_path = sys.argv[1]
    input_path = sys.argv[2]
    db = None
    model: Component = None

    items: dict[str,None] = {}

    with open(input_path,"r") as file:
         temp = file.read()
         db = jsonpickle.decode(temp)
    for key, obj in db.items():
         if type(obj) == Component:
              model = copy.copy(obj)
    assert model != None
    for key, item in model.required.items():
         if type(item) == IpcItem:
            items[key] = [item ,db[item.depl]]
    for key, item in model.provide.items():
         if type(item) == IpcItem:
            items[key] = [item ,db[item.depl]]
    with open(out_path+"/initialization.cc","w") as out_file:
            out_file.write(OUTPUT_FILE_START+CreateLogerInit(model)+CreateDbInit(model,items)+CreatInitCom(model)+OUTPUT_FILE_END)
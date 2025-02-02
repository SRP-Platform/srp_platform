from __future__ import annotations
from tools.model_generator.platform.app.adaptive_application import *
class AdaptiveApplicationRuntimeEnv:
    def CreateLogerInit(app:AdaptiveApplication)->str:
        res = """
Result<void> InitializeLogger() noexcept {
    log::LoggingMenager::Create(\""""+app.logger.GetAppId()+"""\","""+app.logger.GetLogMode()+""","""+app.logger.GetLogLevel()+""");

"""
        have_platform = False
        for ctx in app.logger.ctx_list:
            if ctx.GetCtxId() == "platform-":
                have_platform = True
            res+="""    std::ignore = log::LoggingMenager::GetInstance()->CreateLogger(\""""+ctx.GetCtxId()+"""\", \""""+ctx.ctx_des+"""\", """+ctx.GetLogLevel()+""");
"""
        if not have_platform:
            res +="""
    std::ignore = log::LoggingMenager::GetInstance()->CreateLogger("platform-", "Default ctx for platform", log::LogLevel::kInfo);
"""          
        
        res +="""
    return {};
    }
"""
        
        return res
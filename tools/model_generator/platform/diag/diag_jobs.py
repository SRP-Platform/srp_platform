class diag_job:
    def __init__(self,s_id,sub_id,desc="") -> None:
        self.s_id = s_id
        self.sub_id = sub_id
        self.desc = desc
        pass

class ServiceType:
    def __init__(self,service_id,max_sub_id,min_sub_id, uds_id) -> None:
        self.service_id = service_id
        self.max_sub_id = max_sub_id
        self.min_sub_id = min_sub_id
        self.uds_id = uds_id
        self.skip_sub_service_id = False
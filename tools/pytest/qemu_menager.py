
import asyncio

class QemuMenager:
    def __init__(self):
        self.proc = None
    async def Start(self):
        self.proc = await asyncio.create_subprocess_exec(
            "bazel-7.1.1", "run", "@srp_platform//tools/qemu", "--", "--console",
            cwd="/home/bartek/temp/srp_platform"
        )
    def Stop(self):
        if self.proc:
            print(self.proc.stdout.read())
            print(self.proc.stderr.read())
            self.proc.kill()
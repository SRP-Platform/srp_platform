"""ara_runtime_lib links the MavlinkDaemon client only for an application with on: mavlink."""
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT = "//ara/com/mavlink/client:daemon_client_global"


def deps(path):
    with open(os.path.join(HERE, path), encoding="utf-8") as f:
        return f.read()


class RuntimeDepsTest(unittest.TestCase):
    def test_client_is_linked_with_mavlink_only(self):
        self.assertIn(CLIENT, deps("deps_with_mavlink"))
        self.assertNotIn(CLIENT, deps("plain_app/deps_without_mavlink"))


if __name__ == "__main__":
    unittest.main()

"""Dymne sprawdzenie wyjścia mavgen: wygenerowany moduł pymavlink koduje/dekoduje."""
from __future__ import annotations

import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


def _load():
    spec = importlib.util.spec_from_file_location("srp_radio", os.path.join(HERE, "srp_radio.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class MavgenOutputTest(unittest.TestCase):
    def test_event_roundtrip_with_seq(self):
        m = _load()
        mav = m.MAVLink(None, srcSystem=1, srcComponent=1)
        msg = m.MAVLink_srp_radio_position_message(seq=7, instance=3, lat=1.5, lon=-2.5, fix_type=3)
        frame = msg.pack(mav)
        back = m.MAVLink(None).parse_buffer(frame)
        self.assertEqual(len(back), 1)
        self.assertEqual((back[0].seq, back[0].instance, back[0].lat, back[0].lon, back[0].fix_type),
                         (7, 3, 1.5, -2.5, 3))

    def test_method_resp_header_fields(self):
        m = _load()
        mav = m.MAVLink(None, srcSystem=1, srcComponent=1)
        msg = m.MAVLink_srp_radio_set_mode_resp_message(
            seq=9, instance=4, target_system=2, target_component=1, status=0, value=1)
        back = m.MAVLink(None).parse_buffer(msg.pack(mav))
        self.assertEqual((back[0].seq, back[0].target_system, back[0].status), (9, 2, 0))

    def test_ids(self):
        m = _load()
        self.assertEqual(m.MAVLINK_MSG_ID_SRP_RADIO_POSITION, 200)
        self.assertEqual(m.MAVLINK_MSG_ID_SRP_RADIO_SET_NAME_REQ, 213)

    def test_string_field_len(self):
        m = _load()
        mav = m.MAVLink(None, srcSystem=1, srcComponent=1)
        msg = m.MAVLink_srp_radio_set_name_req_message(
            seq=1, instance=1, target_system=0, target_component=0, value=b"abc")
        back = m.MAVLink(None).parse_buffer(msg.pack(mav))
        self.assertEqual(back[0].value, "abc")


if __name__ == "__main__":
    unittest.main()

"""Testy generatora korpusu fuzz: deterministyczny, pokrywa wszystkie rodzaje, a oczekiwania w manifescie
zgadzaja sie z referencyjnym parserem (pymavlink)."""
import json
import os
import sys

import pytest

import fuzz_corpus as fc
import mav_helpers as mh

CORPUS = fc.build_corpus(seed=1, count=220)


def test_deterministic_per_seed():
    again = fc.build_corpus(seed=1, count=220)
    assert [e["data"] for e in again] == [e["data"] for e in CORPUS]
    assert [e["data"] for e in fc.build_corpus(seed=2, count=220)] != [e["data"] for e in CORPUS]


def test_all_kinds_present_and_count():
    assert len(CORPUS) == 220
    assert {e["kind"] for e in CORPUS} == set(fc.KINDS)


def test_must_decode_frames_are_decoded_in_order_per_version():
    for e in CORPUS:
        if e["kind"] in ("valid", "glued", "mixed_v1_v2", "garbage_prefix", "split_magic", "len255_then_valid"):
            got = fc.known_messages(e)
            want = [(m["ver"], m["msgid"], m["counter"]) for m in e["must_decode"]]
            for ver in (1, 2):  # kolejnosc w ramach wersji (parsery v1 i v2 widza tylko swoje ramki)
                assert [x for x in got if x[0] == ver] == [x for x in want if x[0] == ver], (e["kind"], got, want)
            assert sorted(got) == sorted(want)


def test_corrupted_entries_yield_no_known_messages():
    for e in CORPUS:
        if e["kind"] in ("truncated", "bad_crc", "bad_crc_extra", "random", "bad_len", "magic_garbage",
                         "len255_truncated", "unknown_incompat_flag", "signed_truncated_sig",
                         "signed_flag_without_signature"):
            assert fc.known_messages(e) == [], e["kind"]
            assert e["must_decode"] == []
            if e["kind"] not in ("signed_truncated_sig", "signed_flag_without_signature"):  # tam: optional
                assert e["max_known"] == 0


def test_signed_entries_have_valid_signature_for_given_key():
    seen = 0
    for e in CORPUS:
        if e["kind"] == "signed":
            key = bytes.fromhex(e["signed_key_hex"])
            f = e["data"]
            assert f[2] & 1 and f[-6:] == mh.hmac48(key, f[:-6])
            good, bad = mh.decode_all(mh.load_dialect(2), f, key=key, now_ts=e["now_ts"])
            assert len(good) == 1 and good[0].counter == e["valid_with_key"]["counter"]
            seen += 1
    assert seen >= 10


def test_v1_and_v2_both_used():
    magics = {e["data"][0] for e in CORPUS if e["kind"] in ("valid", "glued")}
    assert magics == {0xFD, 0xFE}
    assert any(e["kind"] == "mixed_v1_v2" and {m["ver"] for m in e["must_decode"]} == {1, 2} for e in CORPUS)


def test_false_magic_kinds_contain_magic_bytes_and_cover_resync():
    mg = [e for e in CORPUS if e["kind"] == "magic_garbage"]
    assert len(mg) >= 10 and all(0xFD in e["data"] or 0xFE in e["data"] for e in mg)
    fh = [e for e in CORPUS if e["kind"] == "fake_header_before_valid"]
    assert len(fh) >= 10
    for e in fh:
        assert e["may_be_lost"] is True and e["data"][0] in (0xFD, 0xFE) and e["max_known"] == 1
        # jawnie: wynik referencyjnego parsera to 0 albo 1 wiadomosc (ramka moze zginac)
        assert len(e["reference_pymavlink"]) <= 1


def test_signed_corruption_entries_have_now_ts_and_are_rejected_with_key():
    seen = {"signed_bad_sig": 0, "signed_truncated_sig": 0, "signed": 0}
    for e in CORPUS:
        if e["kind"] in seen:
            seen[e["kind"]] += 1
            assert isinstance(e["now_ts"], int) and e["signed_key_hex"]
            key = bytes.fromhex(e["signed_key_hex"])
            good, _ = mh.decode_all(mh.load_dialect(2), e["data"], key=key, now_ts=e["now_ts"])
            assert len(good) == (1 if e["kind"] == "signed" else 0), e["kind"]
            if e["kind"] != "signed_truncated_sig":  # ramka kompletna: wyrocznia ze spec zgadza sie z pymavlink
                verdict = mh.StrictVerifier(key, e["now_ts"]).check(e["data"])
                assert (verdict == "ok") == (e["kind"] == "signed"), (e["kind"], verdict)
    assert all(v >= 8 for v in seen.values()), seen


def test_len255_and_unknown_incompat_flag_entries():
    l255 = [e for e in CORPUS if e["kind"] == "len255_then_valid"]
    assert l255 and all(e["data"][1] == 255 and len(e["must_decode"]) == 1 for e in l255)
    inc = [e for e in CORPUS if e["kind"] == "unknown_incompat_flag"]
    assert inc and all(e["data"][2] & ~0x01 and not e["data"][2] & 0x01 for e in inc)
    for e in inc:
        assert fc.known_messages(e) == []  # pymavlink tez odrzuca
    sig = [e for e in CORPUS if e["kind"] == "signed_flag_without_signature"]
    assert sig and all(e["data"][2] == 0x01 and len(e["optional"]) == 1 for e in sig)


def test_v2_zero_tail_entries_have_truncated_len_and_decode_to_original():
    """H8: w korpusie sa ramki v2 z obcietymi zerami (len < pelny payload) i pymavlink odtwarza zera."""
    full = 4 + 4 + 2 + 2 + 2 + 1  # temp, counter, seq, instance, info_offset, state
    zt = [e for e in CORPUS if e["kind"] == "v2_zero_tail"]
    assert len(zt) >= 8
    for e in zt:
        assert e["data"][0] == 0xFD and e["data"][1] < full  # obciete
        good = fc.known_messages(e)
        assert good == [(2, e["must_decode"][0]["msgid"], e["must_decode"][0]["counter"])]
        mav = mh.make_mav(mh.load_dialect(2))[0]
        msg = mav.parse_buffer(e["data"])[0]
        assert getattr(msg, "state", getattr(msg, "status", None)) == 0 and msg.info_offset == 0


def test_manifest_contains_reference_for_every_entry():
    assert all("reference_pymavlink" in e for e in CORPUS)


def test_bad_crc_extra_differs_from_good_only_in_crc():
    g = fc.Gen(__import__("random").Random(3))
    good, _ = g.frame(2, 5)
    g2 = fc.Gen(__import__("random").Random(3))
    bad, _ = g2.frame(2, 5, extra_delta=7)
    assert good[:-2] == bad[:-2] and good[-2:] != bad[-2:]


def test_manual_frames_match_pymavlink_encoding_for_valid():
    g = fc.Gen(__import__("random").Random(4))
    for ver in (1, 2):
        f, m = g.frame(ver, 77)
        good, bad = mh.decode_all(mh.load_dialect(ver), f)
        assert len(good) == 1 and good[0].counter == 77 and not bad


def test_write_corpus_cli_writes_manifest(tmp_path):
    d = str(tmp_path)
    assert fc.main(["--out", d, "--seed", "5", "--count", "33"]) == 0
    man = json.load(open(os.path.join(d, "manifest.json")))
    assert len(man["entries"]) == 33
    for e in man["entries"]:
        assert os.path.getsize(os.path.join(d, e["file"])) > 0


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))

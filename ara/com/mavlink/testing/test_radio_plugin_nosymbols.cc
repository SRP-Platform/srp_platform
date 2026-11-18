/**
 * Copyright (c) 2024
 *
 * @file test_radio_plugin_nosymbols.cc
 * @brief TESTONLY: biblioteka `.so` bez symboli pluginu (nie jest pluginem).
 */
extern "C" __attribute__((visibility("default"))) int srp_unrelated_symbol() { return 1; }

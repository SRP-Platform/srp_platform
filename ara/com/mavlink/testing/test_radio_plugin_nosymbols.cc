/**
 * Copyright (c) 2024
 *
 * @file test_radio_plugin_nosymbols.cc
 * @brief TESTONLY: a `.so` library without plugin symbols (not a plugin).
 */
extern "C" __attribute__((visibility("default"))) int srp_unrelated_symbol() { return 1; }

#!/usr/bin/env python3
"""Tests for overlay-desktop/usr/bin/cloke without any real package manager."""
import importlib.machinery
import importlib.util
import os
import sys
import unittest
from unittest import mock

path = os.path.join(os.path.dirname(__file__), "..", "overlay-desktop", "usr", "bin", "cloke")
loader = importlib.machinery.SourceFileLoader("cloke", path)
cloke = importlib.util.module_from_spec(importlib.util.spec_from_loader("cloke", loader))
loader.exec_module(cloke)


class Cloke(unittest.TestCase):
    def setUp(self):
        self.p = [mock.patch.object(cloke, "HOST", "macos"), mock.patch.object(cloke, "have", lambda b: b == "brew"),
                  mock.patch.object(cloke, "appimage_feed", lambda: []),
                  mock.patch.object(cloke, "layer_available", lambda l: False),
                  mock.patch.object(cloke, "OS_ORDER", ["macos", "linux", "windows"])]
        for x in self.p:
            x.start()
        self.addCleanup(lambda: [x.stop() for x in self.p])

    def test_steam_lists_all_oses_and_recommends_linux_dnf(self):
        with mock.patch.object(cloke, "run", lambda *a, **k: "steam\n" if "--cask" in a[0] else ""):
            picks = cloke.find("steam")
        self.assertEqual([p.label() for p in picks], ["Steam-macOS (brew, cask)", "Steam-Linux (dnf)", "Steam-Linux (flatpak)",
                                                      "Steam-Windows (winget)", "Steam-Windows (choco)"])
        self.assertEqual(cloke.recommended(picks), 1)

    def test_formula_beats_everything(self):
        with mock.patch.object(cloke, "run", lambda *a, **k: "git\n" if "--formula" in a[0] else ""):
            picks = cloke.find("git")
        self.assertEqual(picks[cloke.recommended(picks)].label(), "Git-macOS (brew)")

    def test_unknown_program_only_live_hits(self):
        with mock.patch.object(cloke, "run", lambda *a, **k: "figlet\nfiglet-fonts\n" if "--formula" in a[0] else ""):
            picks = cloke.find("figlet")
        self.assertEqual([p.id for p in picks], ["figlet"])

    def test_winget_table_parser(self):
        out = "Name  Id          Version Source\n-----------------------------\nSteam Valve.Steam 1.0   winget\n"
        with mock.patch.object(cloke, "run", lambda *a, **k: out):
            pk = cloke.search_winget("steam")
        self.assertEqual((pk[0].id, pk[0].version), ("Valve.Steam", "1.0"))

    def test_dnf_and_flatpak_parsers(self):
        with mock.patch.object(cloke, "run", lambda *a, **k: "steam.x86_64 : Valve client\nsteam-devices.noarch : udev\n"):
            self.assertEqual(cloke.search_dnf("steam")[0].id, "steam")
        with mock.patch.object(cloke, "run", lambda *a, **k: "com.valvesoftware.Steam\tSteam\tclient\n"):
            self.assertEqual(cloke.search_flatpak("steam")[0].id, "com.valvesoftware.Steam")

    def test_missing_layer_is_reported_not_run(self):
        pk = cloke.Pkg("dnf", "steam", "steam")
        with mock.patch.object(cloke.subprocess, "call") as call:
            self.assertEqual(cloke.do_install(pk), 2)
            call.assert_not_called()


if __name__ == "__main__":
    unittest.main()

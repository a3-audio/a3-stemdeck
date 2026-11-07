# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""stemdeck-aes67-deck: a deck host's AES67 configs, rendered from the truth.

A deck host (a3nuc2) has no a3-core, so the stemdeck package renders both of
its PipeWire AES67 instances: the base (PTP clock, receives the Core's
stream) and one sender per deck, which joins the base's pipewire.ptp0 group
and has no PTP driver of its own (plan ruling R4)."""

import importlib.machinery
import importlib.util
import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TOOL = REPO / "tools/stemdeck-aes67-deck"
_loader = importlib.machinery.SourceFileLoader("stemdeck_aes67_deck", str(TOOL))
_spec = importlib.util.spec_from_loader("stemdeck_aes67_deck", _loader)
deck = importlib.util.module_from_spec(_spec)
_loader.exec_module(deck)

# Exactly the keys a3-core's truth carries for this (C1, feat/deck-b-core).
TRUTH = {
    "network": {"audio_port": "enp5s0"},
    "hosts": {
        "aes67-core": "239.69.150.243",
        "aes67-deck-a": "239.69.150.244",
        "aes67-deck-b": "239.69.150.245",
        "sap": "239.255.255.255",
    },
    "listeners": [
        {"name": "aes67.stream", "program": "aes67", "role": "stream",
         "host": "aes67-core", "port": 5004},
        {"name": "aes67.deck", "program": "aes67", "role": "deck",
         "host": "aes67-deck-{deck}", "port": 5004},
        {"name": "aes67.sap", "program": "aes67", "role": "sap", "host": "sap", "port": 9875},
    ],
}

NAMES = ([f"MAIN{n}" for n in range(1, 17)] + [f"AUX{n}" for n in range(1, 17)]
         + [f"CUE{n}" for n in range(1, 10)])


class RenderCase(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.home = Path(tmp.name) / "home"
        self.home.mkdir()
        self.truth_file = Path(tmp.name) / "a3-osc.json"
        self.write_truth(TRUTH)

    def write_truth(self, truth):
        self.truth_file.write_text(json.dumps(truth))

    def run_tool(self, *args, env=None):
        environ = {k: v for k, v in os.environ.items()
                   if k not in ("A3_OSC_TRUTH", "STEMDECK_AUDIO_PORT")}
        environ.update(env or {})
        return subprocess.run([TOOL, *args, "--home", str(self.home)],
                              capture_output=True, text=True, env=environ)

    def render(self, *args):
        return self.run_tool(*args, "--truth", str(self.truth_file))

    def pipewire_dir(self):
        return self.home / ".config/pipewire"

    def written(self):
        folder = self.pipewire_dir()
        return sorted(p.name for p in folder.iterdir()) if folder.exists() else []


class TheDeckSender(RenderCase):
    def conf(self, letter="B"):
        return (self.pipewire_dir() / f"pipewire-aes67-deck-{letter}.conf").read_text()

    def test_render_deck_b_from_the_truth(self):
        done = self.render("render", "B")
        self.assertEqual(0, done.returncode, done.stderr)
        text = self.conf()
        for line in ('local.ifname = "enp5s0"',
                     'destination.ip = "239.69.150.245"',
                     "destination.port = 5004",
                     "sess.min-ptime = 0.25",
                     "sess.max-ptime = 0.25",
                     'sess.name = "AES67 deck B"',
                     'audio.format = "S24BE"',
                     "audio.rate = 44100",
                     "audio.channels = 41",
                     "sess.latency.msec = 1",
                     "net.mtu = 1500",
                     'node.name = "aes67-deck-b"',
                     'node.group = "pipewire.ptp0"',
                     "sess.sap.announce = true",
                     "node.always-process = true",
                     'sap.ip = "239.255.255.255"',
                     "sap.port = 9875"):
            with self.subTest(line=line):
                self.assertIn(line, text)
        names = "node.channel-names = [" + ", ".join(f'"{n}"' for n in NAMES) + "]"
        self.assertIn(names, text)
        self.assertEqual(41, len(NAMES))

    def test_the_sender_has_no_ptp_driver_of_its_own(self):
        """Two PTP0 drivers in one graph would fight (R4): the base owns it."""
        self.render("render", "B")
        text = self.conf()
        self.assertNotIn("PTP0-Driver", text)
        self.assertNotIn("support.node.driver", text)

    def test_its_sap_module_only_announces(self):
        self.render("render", "B")
        text = self.conf()
        self.assertIn("libpipewire-module-rtp-sap", text)
        self.assertIn("announce-stream", text)
        self.assertNotIn("create-stream", text)
        self.assertNotIn("libpipewire-module-rtp-source", text)

    def test_nothing_is_left_unfilled(self):
        self.render("render", "B")
        self.assertIsNone(re.search(r"@[A-Z_]+@", self.conf()))

    def test_a_lowercase_letter_is_the_same_deck(self):
        self.assertEqual(0, self.render("render", "b").returncode)
        self.assertEqual(["pipewire-aes67-deck-B.conf"], self.written())

    def test_any_letter_renders_its_own_sender(self):
        """Ruling A8: no fixed deck count. A letter the truth names no group
        for counts on from deck A's group, one address a letter, carried as a
        32-bit number: C, D follow B, Z crosses into the next /24."""
        for letter, address in (("C", "239.69.150.246"), ("D", "239.69.150.247"),
                                ("L", "239.69.150.255"), ("M", "239.69.151.0"),
                                ("Z", "239.69.151.13")):
            with self.subTest(letter=letter):
                for step in ("can-render", "render"):
                    done = self.render(step, letter.lower())
                    self.assertEqual(0, done.returncode, done.stderr)
                text = self.conf(letter)
                self.assertIn(f'destination.ip = "{address}"', text)
                self.assertIn(f'sess.name = "AES67 deck {letter}"', text)
                self.assertIn(f'node.name = "aes67-deck-{letter.lower()}"', text)
                self.assertIsNone(re.search(r"@[A-Z_]+@", text))

    def test_every_letter_has_a_group_of_its_own(self):
        truth = deck.load_truth(self.truth_file)
        groups = {deck.deck_group(truth, chr(c))[0] for c in range(ord("A"), ord("Z") + 1)}
        self.assertEqual(26, len(groups))
        self.assertTrue(groups.isdisjoint({"239.69.150.243", "239.255.255.255"}))

    def test_a_group_the_truth_names_wins(self):
        self.write_truth({**TRUTH, "hosts": {**TRUTH["hosts"], "aes67-deck-c": "239.69.160.1"}})
        self.assertEqual(0, self.render("render", "C").returncode)
        self.assertIn('destination.ip = "239.69.160.1"', self.conf("C"))

    def test_a_counted_group_on_another_host_s_address_is_refused(self):
        self.write_truth({**TRUTH, "hosts": {**TRUTH["hosts"], "other": "239.69.150.246"}})
        done = self.render("render", "C")
        self.assertEqual(2, done.returncode)
        self.assertIn("239.69.150.246", done.stderr)
        self.assertEqual([], self.written())

    def test_without_deck_a_s_group_a_letter_has_none(self):
        hosts = {k: v for k, v in TRUTH["hosts"].items() if k != "aes67-deck-a"}
        self.write_truth({**TRUTH, "hosts": hosts})
        done = self.render("render", "C")
        self.assertEqual(2, done.returncode)
        self.assertIn("aes67-deck-a", done.stderr)
        self.assertEqual([], self.written())

    def test_render_refuses_what_is_no_letter(self):
        for word in ("BB", "1", ""):
            with self.subTest(word=word):
                self.assertEqual(2, self.render("render", word).returncode)
        self.assertEqual([], self.written())

    def test_render_without_audio_port_writes_nothing(self):
        self.write_truth({**TRUTH, "network": {}})
        done = self.render("render", "B")
        self.assertNotEqual(0, done.returncode)
        self.assertIn("audio_port", done.stderr)
        self.assertEqual([], self.written())

    def test_without_audio_port_an_old_render_goes(self):
        self.render("render", "B")
        self.write_truth({**TRUTH, "network": {"audio_port": ""}})
        self.render("render", "B")
        self.assertEqual([], self.written())

    def test_the_host_s_own_audio_port_wins(self):
        """P28: the served truth names the Core's socket; the host knows its own."""
        done = self.run_tool("render", "B", "--truth", str(self.truth_file),
                             env={"STEMDECK_AUDIO_PORT": "eno2"})
        self.assertEqual(0, done.returncode, done.stderr)
        self.assertIn('local.ifname = "eno2"', self.conf())
        self.assertNotIn("enp5s0", self.conf())

    def test_an_empty_host_audio_port_falls_back_to_the_truth(self):
        self.run_tool("render", "B", "--truth", str(self.truth_file),
                      env={"STEMDECK_AUDIO_PORT": ""})
        self.assertIn('local.ifname = "enp5s0"', self.conf())

    def test_the_host_s_audio_port_without_one_in_the_truth(self):
        self.write_truth({**TRUTH, "network": {}})
        done = self.run_tool("render", "B", "--truth", str(self.truth_file),
                             env={"STEMDECK_AUDIO_PORT": "eno2"})
        self.assertEqual(0, done.returncode, done.stderr)
        self.assertIn('local.ifname = "eno2"', self.conf())

    def test_a_truth_without_the_deck_listener_is_refused(self):
        self.write_truth({**TRUTH, "listeners": TRUTH["listeners"][2:]})
        done = self.render("render", "B")
        self.assertEqual(2, done.returncode)
        self.assertIn("aes67.deck", done.stderr)
        self.assertEqual([], self.written())


DECK_ENTRY = {"key": "deck", "session": "AES67 deck {deck}", "sender": "deck",
              "group_base": "239.69.150.244", "group_step": 1, "port": 5004,
              "ptime": 0.25, "source": "fixed", "channels": 41, "legacy": True}


class TheStreamTable(RenderCase):
    def with_table(self, **changes):
        self.write_truth({**TRUTH, "aes67_streams": [{**DECK_ENTRY, **changes}]})
        return deck.load_truth(self.truth_file)

    def test_the_table_gives_the_decks_session_group_and_ptime(self):
        stream = deck.deck_stream(self.with_table(), "C")
        self.assertEqual({"session": "AES67 deck C", "group": "239.69.150.246",
                          "port": 5004, "ptime": 0.25}, stream)

    def test_a_truth_without_the_table_renders_exactly_as_before(self):
        for letter, group in (("A", "239.69.150.244"), ("B", "239.69.150.245"),
                              ("C", "239.69.150.246")):
            with self.subTest(letter=letter):
                stream = deck.deck_stream(TRUTH, letter)
                self.assertEqual({"session": f"AES67 deck {letter}", "group": group,
                                  "port": 5004, "ptime": 0.25}, stream)
                self.assertEqual(0, self.render("render", letter).returncode)
                text = (self.pipewire_dir() / f"pipewire-aes67-deck-{letter}.conf").read_text()
                self.assertIn(f'destination.ip = "{group}"', text)
                self.assertIn("sess.min-ptime = 0.25", text)
                self.assertIn(f'sess.name = "AES67 deck {letter}"', text)

    def test_a_table_without_the_deck_entry_falls_back(self):
        truth = {**TRUTH, "aes67_streams": [{**DECK_ENTRY, "key": "other"}]}
        self.assertEqual("239.69.150.245", deck.deck_stream(truth, "B")["group"])

    def test_the_table_and_the_old_hosts_agree_for_a_b_c(self):
        with_table = self.with_table()
        for letter in "ABC":
            self.assertEqual(deck.deck_stream(TRUTH, letter),
                             deck.deck_stream(with_table, letter))

    def test_a_table_with_a_moved_base_moves_the_group(self):
        self.with_table(group_base="239.69.150.250")
        self.assertEqual(0, self.render("render", "B").returncode)
        text = (self.pipewire_dir() / "pipewire-aes67-deck-B.conf").read_text()
        self.assertIn('destination.ip = "239.69.150.251"', text)

    def test_a_deck_letter_beyond_the_old_hosts_still_resolves(self):
        stream = deck.deck_stream(self.with_table(), "AA")
        self.assertEqual("239.69.151.14", stream["group"])
        self.assertEqual("AES67 deck AA", stream["session"])

    def test_a_malformed_deck_entry_renders_as_without_the_table(self):
        self.assertEqual(0, self.render("render", "B").returncode)
        before = (self.pipewire_dir() / "pipewire-aes67-deck-B.conf").read_text()
        without_base = {k: v for k, v in DECK_ENTRY.items() if k != "group_base"}
        for name, entry in (("no group_base", without_base),
                            ("a step that is no number", {**DECK_ENTRY, "group_step": "x"}),
                            ("a base that is no address", {**DECK_ENTRY, "group_base": "239.69.150"}),
                            ("40 channels", {**DECK_ENTRY, "channels": 40})):
            with self.subTest(name):
                self.write_truth({**TRUTH, "aes67_streams": [entry]})
                result = self.render("render", "B")
                self.assertEqual(0, result.returncode, result.stderr)
                text = (self.pipewire_dir() / "pipewire-aes67-deck-B.conf").read_text()
                self.assertEqual(before, text)
                self.assertEqual(1, len(result.stderr.splitlines()), result.stderr)
                self.assertIn("aes67_streams", result.stderr)

    def test_the_tables_ptime_reaches_the_conf(self):
        self.with_table(ptime=1)
        self.assertEqual(0, self.render("render", "B").returncode)
        text = (self.pipewire_dir() / "pipewire-aes67-deck-B.conf").read_text()
        self.assertIn("sess.min-ptime = 1", text)
        self.assertIn("sess.max-ptime = 1", text)


class TheBase(RenderCase):
    """The deck host's own AES67 instance: PTP client clock, receiver."""

    def conf(self):
        return (self.pipewire_dir() / "pipewire-stemdeck-aes67.conf").read_text()

    def test_render_base_from_the_truth(self):
        done = self.render("render-base")
        self.assertEqual(0, done.returncode, done.stderr)
        text = self.conf()
        for line in ("node.name       = PTP0-Driver",
                     "node.group      = pipewire.ptp0",
                     'clock.interface = "enp5s0"',
                     'local.ifname = "enp5s0"',
                     'sap.ip = "239.255.255.255"',
                     "sap.port = 9875",
                     # the Core's stream, "AES67 a3nuc1 out", in the PTP group
                     'rtp.session = "~ out$"',
                     "node.group = pipewire.ptp0",
                     "default.clock.rate          = 44100",
                     "default.clock.quantum       = 64",
                     "default.clock.min-quantum   = 64"):
            with self.subTest(line=line):
                self.assertIn(line, text)

    def test_no_receive_rule_matches_on_sess_name(self):
        # PipeWire 1.6.9 module-rtp-sap matches stream.rules against the
        # properties of the *received* session (rtp.session = the SDP s= name,
        # node.name, media.name, rtp.*); there is no sess.name, so such a rule
        # never matches and no stream is created (found on the rig 2026-10-28).
        self.render("render-base")
        self.assertNotIn("sess.name", self.conf())

    def test_the_base_sends_nothing(self):
        """The Core's sender owns aes67.stream's group; a second one there collides."""
        self.render("render-base")
        text = self.conf()
        self.assertNotIn("libpipewire-module-rtp-sink", text)
        self.assertNotIn("239.69.150.243", text)

    def test_the_base_does_not_receive_deck_streams(self):
        self.render("render-base")
        self.assertNotIn("AES67 deck", self.conf())

    def test_nothing_is_left_unfilled(self):
        self.render("render-base")
        self.assertIsNone(re.search(r"@[A-Z_]+@", self.conf()))

    def test_render_base_without_audio_port_writes_nothing(self):
        self.write_truth({**TRUTH, "network": {}})
        done = self.render("render-base")
        self.assertNotEqual(0, done.returncode)
        self.assertEqual([], self.written())

    def test_the_host_s_own_audio_port_wins(self):
        done = self.run_tool("render-base", "--truth", str(self.truth_file),
                             env={"STEMDECK_AUDIO_PORT": "eno2"})
        self.assertEqual(0, done.returncode, done.stderr)
        self.assertIn('clock.interface = "eno2"', self.conf())
        self.assertIn('local.ifname = "eno2"', self.conf())
        self.assertNotIn("enp5s0", self.conf())

    def test_without_any_audio_port_the_base_writes_nothing(self):
        self.write_truth({**TRUTH, "network": {}})
        done = self.run_tool("render-base", "--truth", str(self.truth_file),
                             env={"STEMDECK_AUDIO_PORT": ""})
        self.assertEqual(1, done.returncode)
        self.assertEqual([], self.written())

    def test_its_name_is_not_a3_cores(self):
        """a3nuc1 carries both packages: a3-core renders pipewire-aes67.conf."""
        self.render("render-base")
        self.assertNotIn("pipewire-aes67.conf", self.written())


class WhichHostRunsTheBase(RenderCase):
    """`base-wanted`: the unit's ExecCondition. Where a3-core's AES67 unit is,
    that instance owns PTP0 and the base must not start beside it."""

    def wanted(self, root=None):
        return deck.base_wanted(self.home, root or self.home / "root")

    def test_a_host_without_a3_cores_unit_runs_the_base(self):
        self.assertTrue(self.wanted())

    def test_a3_cores_unit_in_any_place_keeps_the_base_off(self):
        root = self.home / "root"
        for place in (self.home / ".config/systemd/user/a3-aes67.service",
                      self.home / ".local/share/a3-core/config/systemd/user/a3-aes67.service",
                      root / "usr/lib/systemd/user/a3-aes67.service"):
            with self.subTest(place=place):
                place.parent.mkdir(parents=True, exist_ok=True)
                place.write_text("[Unit]\n")
                self.assertFalse(self.wanted(root))
                place.unlink()

    def test_the_condition_exits_0_or_1(self):
        done = self.run_tool("base-wanted")
        self.assertEqual(0, done.returncode, done.stderr)
        unit = self.home / ".config/systemd/user/a3-aes67.service"
        unit.parent.mkdir(parents=True)
        unit.write_text("[Unit]\n")
        done = self.run_tool("base-wanted")
        self.assertEqual(1, done.returncode)
        self.assertIn("a3-aes67.service", done.stdout + done.stderr)


class WhichTruth(RenderCase):
    """StemDeck's own order (Source/OscTruthFile.h): $A3_OSC_TRUTH, else what
    Core last served if it reads as a truth, else the package's file."""

    def setUp(self):
        super().setUp()
        self.package = self.home / "package-a3-osc.json"
        self.cache = self.home / ".cache/a3/a3-osc.json"

    def path(self, environ=None):
        return deck.truth_path(None, self.home, environ or {}, self.package)

    def test_the_cache_first(self):
        self.cache.parent.mkdir(parents=True)
        self.cache.write_text(json.dumps(TRUTH))
        self.assertEqual(self.cache, self.path())

    def test_a_cache_that_is_no_truth_is_passed_over(self):
        self.cache.parent.mkdir(parents=True)
        self.cache.write_text("{half")
        self.assertEqual(self.package, self.path())

    def test_the_package_without_a_cache(self):
        self.assertEqual(self.package, self.path())

    def test_the_environment_over_everything(self):
        self.assertEqual(Path("/x.json"), self.path({"A3_OSC_TRUTH": "/x.json"}))

    def test_render_reads_the_cache(self):
        self.cache.parent.mkdir(parents=True)
        self.cache.write_text(json.dumps(TRUTH))
        self.assertEqual(0, self.run_tool("render", "B").returncode)


class CanRender(RenderCase):
    """`can-render <X>` / `can-render-base`: the units' ExecCondition. A refusal
    skips the unit (exit 1) instead of failing it: systemd retries a failed
    ExecStartPre whatever RestartPreventExitStatus says (measured, v262)."""

    REFUSALS = (
        ("no audio port", {"network": {}}, "B"),
        ("no group for the letter and none to count from",
         {"hosts": {k: v for k, v in TRUTH["hosts"].items() if k != "aes67-deck-a"}}, "C"),
        ("no deck listener", {"listeners": TRUTH["listeners"][2:]}, "B"),
        ("no sap listener", {"listeners": TRUTH["listeners"][:2]}, "B"),
        ("no sap host", {"hosts": {k: v for k, v in TRUTH["hosts"].items() if k != "sap"}}, "B"),
    )

    def check(self, step, *args):
        return self.run_tool(step, *args, "--truth", str(self.truth_file))

    def test_a_renderable_deck_and_base_pass(self):
        for letter in ("B", "C", "D", "Z"):
            with self.subTest(letter=letter):
                self.assertEqual(0, self.check("can-render", letter).returncode)
        self.assertEqual(0, self.check("can-render-base").returncode)
        self.assertEqual([], self.written())

    def test_can_render_agrees_with_render_on_every_refusal(self):
        for why, change, letter in self.REFUSALS:
            with self.subTest(why=why):
                self.write_truth({**TRUTH, **change})
                self.assertNotEqual(0, self.render("render", letter).returncode)
                done = self.check("can-render", letter)
                self.assertEqual(1, done.returncode)
                self.assertTrue(done.stdout.strip())
                self.assertEqual([], self.written())

    def test_can_render_base_agrees_with_render_base(self):
        for why, change in (("no audio port", {"network": {}}),
                            ("no sap listener", {"listeners": TRUTH["listeners"][:2]})):
            with self.subTest(why=why):
                self.write_truth({**TRUTH, **change})
                self.assertNotEqual(0, self.render("render-base").returncode)
                self.assertEqual(1, self.check("can-render-base").returncode)
                self.assertEqual([], self.written())

    def test_what_is_no_letter_is_skipped_not_failed(self):
        for word in ("BB", "1", ""):
            with self.subTest(word=word):
                self.assertEqual(1, self.check("can-render", word).returncode)

    def test_a_missing_truth_is_skipped(self):
        self.truth_file.unlink()
        self.assertEqual(1, self.check("can-render", "B").returncode)
        self.assertEqual(1, self.check("can-render-base").returncode)

    def test_the_host_s_audio_port_counts(self):
        self.write_truth({**TRUTH, "network": {}})
        done = self.run_tool("can-render", "B", "--truth", str(self.truth_file),
                             env={"STEMDECK_AUDIO_PORT": "eno2"})
        self.assertEqual(0, done.returncode)


class UsageErrors(RenderCase):
    def test_an_unknown_step_is_refused(self):
        self.assertEqual(2, self.run_tool("nonsense").returncode)


if __name__ == "__main__":
    unittest.main()

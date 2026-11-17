"""On a machine without the Core, a patchbay wires StemDeck to AES67.

a3nuc2 runs StemDeck on its own: its audio reaches the Core over AES67, which
PipeWire offers to JACK as two clients. The sender "AES67 a3nuc2 stems" takes
send_D1L..send_AUXR (the ten bus channels, the inputs the Core's local
StemDeck uses for deck1_L..aux_R, in that order) and send_CUEL / send_CUER
(the cue pair); the receiver "AES67 Core REC" returns receive_L / R,
the Core's rec bus. Nothing connects StemDeck to either by
itself; the patchbay makes it permanent, and its unit keeps a QjackCtl
running with it.

QjackCtl connects a cable plug by plug, in order, and reads a socket's client
as a regular expression; both are checked against the names the programs
really register.
"""

import configparser
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PATCHBAY = REPO / ".config/rncbc.org/stemdeck-without-core.xml"
UNIT = REPO / ".config/systemd/user/qjackctl-stemdeck.service"
UNITS = REPO / ".config/systemd/user"
PACKAGED_TOOLS = "/usr/lib/stemdeck"
INSTALLED_PATCHBAY = "/usr/share/stemdeck/stemdeck-without-core.xml"

# StemDeck's buses toward the Core, in Buses.h's order: 1-4, then AUX.
TO_THE_CORE = ["deck1", "deck2", "deck3", "deck4", "aux"]
SENDS = ["D1", "D2", "D3", "D4", "AUX"]
SENDER = "AES67 a3nuc2 stems"
RECEIVER = "AES67 Core REC"
STEMDECK_TO_SENDER = [(f"{bus}_{side}", f"send_{send}{side}")
                      for bus, send in zip(TO_THE_CORE, SENDS) for side in "LR"]
CUE_TO_SENDER = [("cue_L", "send_CUEL"), ("cue_R", "send_CUER")]
RECEIVER_TO_STEMDECK = [("receive_L", "rec_L"), ("receive_R", "rec_R")]


def connections():
    """Every (output client, port, input client, port) the patchbay makes."""
    root = ET.parse(PATCHBAY).getroot()
    outputs = {s.get("name"): s for s in root.find("output-sockets")}
    inputs = {s.get("name"): s for s in root.find("input-sockets")}
    made = []
    for cable in root.find("cables"):
        out, into = outputs[cable.get("output")], inputs[cable.get("input")]
        out_plugs = [p.text for p in out.findall("plug")]
        in_plugs = [p.text for p in into.findall("plug")]
        assert len(out_plugs) == len(in_plugs), cable.attrib
        made += [(out.get("client"), o, into.get("client"), i)
                 for o, i in zip(out_plugs, in_plugs)]
    return made


def real(client_pattern, name):
    return re.fullmatch(client_pattern, name) is not None


class ThePatchbayIsQjackCtls(unittest.TestCase):
    def test_it_is_a_qjackctl_patchbay(self):
        text = PATCHBAY.read_text()
        self.assertTrue(text.startswith("<!DOCTYPE patchbay>"), text[:40])
        root = ET.fromstring(text)
        self.assertEqual("patchbay", root.tag)
        self.assertEqual("stemdeck-without-core", root.get("name"))
        self.assertTrue(root.get("version"))
        for part in ("output-sockets", "input-sockets", "slots", "cables"):
            self.assertIsNotNone(root.find(part), part)

    def test_every_socket_is_audio_and_not_exclusive(self):
        root = ET.parse(PATCHBAY).getroot()
        for socket in root.iter("socket"):
            self.assertEqual("jack-audio", socket.get("type"), socket.attrib)
            self.assertEqual("off", socket.get("exclusive"), socket.attrib)

    def test_every_cable_joins_sockets_of_equal_size(self):
        connections()


class TheCables(unittest.TestCase):
    def test_exactly_stemdeck_to_the_sender_and_the_receiver_back(self):
        """The two channels back are REAPER's rec bus (where its patchbay
        feeds them into StemDeck:rec_L/R): StemDeck records the rig's mix
        here too (maintainer, 2026-10-06)."""
        expected = [("StemDeck", o, SENDER, i) for o, i in STEMDECK_TO_SENDER]
        expected += [("StemDeck", o, SENDER, i) for o, i in CUE_TO_SENDER]
        expected += [(RECEIVER, o, "StemDeck", i) for o, i in RECEIVER_TO_STEMDECK]
        made = connections()
        self.assertEqual(len(expected), len(made), made)
        for (o_client, o, i_client, i), (o_pat, o_got, i_pat, i_got) in zip(expected, made):
            self.assertTrue(real(o_pat, o_client), (o_pat, o_client))
            self.assertTrue(real(i_pat, i_client), (i_pat, i_client))
            self.assertEqual((o, i), (o_got, i_got))

    def test_the_order_is_the_cores(self):
        """deck1_L..aux_R on send_D1L..send_AUXR: the sender's channels land
        on REAPER in13..in22, where its local StemDeck puts deck1_L..aux_R."""
        sent = [(o, i) for _, o, c, i in connections() if real(c, SENDER)]
        self.assertEqual(STEMDECK_TO_SENDER + CUE_TO_SENDER, sent)

    def test_the_cue_goes_to_the_cue_channels(self):
        self.assertEqual(
            [("cue_L", "send_CUEL"), ("cue_R", "send_CUER")],
            [(o, i) for _, o, c, i in connections()
             if o.startswith("cue_") and real(c, SENDER)])

    def test_nothing_goes_through_zita(self):
        for out_client, _, in_client, _ in connections():
            self.assertNotIn("zita", out_client)
            self.assertNotIn("zita", in_client)


class TheNamesAreTheProgramsOwn(unittest.TestCase):
    def test_stemdeck_registers_these_ports(self):
        """Outputs.h: buses::portName(channel / 2) + _L/_R, then cue_L/cue_R
        after the ten bus ports; Buses.h: deck1..4 for buses 0..3, aux for
        bus 4; the client is "StemDeck"."""
        buses = (REPO / "Source/Buses.h").read_text()
        outputs = (REPO / "Source/Outputs.h").read_text()
        main = (REPO / "Source/MainComponent.cpp").read_text()
        self.assertIn("constexpr int aux = 4;", buses)
        self.assertIn('bus == aux ? "aux"', buses)
        self.assertIn('"deck" + std::to_string (bus + 1)', buses)
        self.assertIn('channel % 2 == 0 ? "_L" : "_R"', outputs)
        self.assertIn('channel >= cueChannel() ? std::string ("cue") + side'
                      ' : buses::portName (channel / 2) + side', outputs)
        self.assertIn("cueChannel() { return busChannelCount(); }", outputs)
        self.assertIn("channelCount() { return cueChannel() + 2; }", outputs)
        # The client is opened through AudioOutput (shared with -bformat).
        self.assertIn('audio->open ("StemDeck"', main)

    def test_every_stemdeck_port_in_the_patchbay_exists(self):
        """Five stereo buses and the cue pair are what StemDeck puts out; no
        phones ports since 2026-10-07 (the cue is the desk channel, in
        REAPER)."""
        buses = (REPO / "Source/Buses.h").read_text()
        self.assertIn("constexpr int count = 5;", buses)
        self.assertNotIn("phones", buses)
        outputs = [o for c, o, *_ in connections()
                   if real(c, "StemDeck") and not o.startswith("rec_")]
        self.assertEqual(2 * 5 + 2, len(outputs))

    def test_the_aes67_names_are_the_streams(self):
        """The PipeWire streams' JACK client names and ports, as registered on
        a3nuc2 (sender, 12 inputs) and from the Core (receiver, 2 outputs)."""
        sender_ports = {i for _, _, c, i in connections() if real(c, SENDER)}
        self.assertTrue(sender_ports <= {
            f"send_{n}{s}" for n in SENDS + ["CUE"] for s in "LR"})
        receiver_ports = {o for c, o, *_ in connections() if real(c, RECEIVER)}
        self.assertTrue(receiver_ports <= {"receive_L", "receive_R"})


class TheStreamsFitAnyMachine(unittest.TestCase):
    """The AES67 streams are named after their machine ("AES67 <host> stems";
    the Core's are "AES67 Core ..."), so the patchbay matches them by pattern, never by
    one machine's name (decided 2026-10-14)."""

    def aes67_clients(self):
        root = ET.parse(PATCHBAY).getroot()
        return [s.get("client") for s in root.iter("socket")
                if s.get("client").replace("\\", "").startswith("AES67")]

    def test_no_machine_is_named(self):
        for client in self.aes67_clients():
            self.assertNotIn("a3nuc", client)

    def test_the_receiver_is_only_the_rec_stream(self):
        # MAIN/BOOTH/PHONES must never match: they would be wired to rec_L/R
        self.assertFalse(real(RECEIVER, "AES67 Core MAIN"))
        self.assertFalse(real(RECEIVER, "AES67 Core PHONES"))

    def test_another_machines_streams_are_matched_too(self):
        for client in self.aes67_clients():
            self.assertTrue(real(client, "AES67 studio-2 stems")
                            or real(client, "AES67 Core REC"), client)


def section(name):
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.optionxform = str
    parser.read_string(UNIT.read_text())
    return parser[name]


def exec_lines(*keys):
    return [line.split("=", 1)[1] for line in UNIT.read_text().splitlines()
            if line.split("=", 1)[0] in keys]


class ItsUnit(unittest.TestCase):
    def test_it_runs_qjackctl_with_this_patchbay_active(self):
        self.assertEqual([f"/usr/bin/qjackctl -a {INSTALLED_PATCHBAY}"],
                         exec_lines("ExecStart"))

    def test_it_never_starts_a_server_itself(self):
        """-s would start JACK from QjackCtl's settings; the machine's JACK is
        whatever already runs there."""
        for line in exec_lines("ExecStart"):
            self.assertNotIn(" -s", line)
            self.assertNotIn("--start", line)

    def test_it_waits_for_the_screen_and_for_jack(self):
        self.assertEqual([f"{PACKAGED_TOOLS}/a3-wait-for-the-screen",
                          "/usr/bin/jack_wait -w"], exec_lines("ExecStartPre"))
        self.assertEqual("DISPLAY=:0", section("Service").get("Environment"))

    def test_it_stops_only_its_own_process(self):
        """No pkill: systemd stops the unit's own process; a pattern would
        hit any QjackCtl on the machine."""
        self.assertEqual([], exec_lines("ExecStop", "ExecStopPost"))
        self.assertNotIn("pkill", UNIT.read_text())

    def test_it_comes_back_and_starts_at_login(self):
        self.assertEqual("on-failure", section("Service").get("Restart"))
        self.assertIn("default.target", section("Install").get("WantedBy", "").split())

    def test_it_needs_no_unit_of_the_core(self):
        for key in ("BindsTo", "Requires", "Requisite"):
            self.assertNotIn("a3-", section("Unit").get(key, ""), key)

    def test_its_patchbay_is_the_packaged_copy_of_this_repository_s(self):
        self.assertEqual(PATCHBAY.name, Path(INSTALLED_PATCHBAY).name)
        self.assertTrue(PATCHBAY.is_file())


if __name__ == "__main__":
    unittest.main()

"""The stemdeck package: its control file, its maintainer scripts, its tree.

The package writes only to /usr. postinst enables the user unit for every
user and does nothing else -- no write into any home, no restart (spec
app-packages, 2026-10-07). User data stays on purge."""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEBIAN = REPO / "packaging/DEBIAN"
STAGE = REPO / "packaging/stage"
UNITS = REPO / ".config/systemd/user"
UNIT = UNITS / "stemdeck.service"
TEMPLATE = REPO / ".config/systemd/user/stemdeck@.service"
EXTRA_DEPENDS = ("jack-example-tools", "xdotool", "x11-xserver-utils", "x11-utils",
                 "python3", "onboard", "pipewire-jack")
TOOLS = ("a3-wait-for-the-screen", "rig-keep-the-screen.sh", "setup-separator.sh",
         "stemdeck-seed", "stemdeck-aes67-deck")
AES67_UNITS = ("a3-aes67-deck@.service", "stemdeck-aes67.service")
AES67_TEMPLATES = ("pipewire-aes67-deck.conf.in", "pipewire-stemdeck-aes67.conf.in")
EEE = "etc/systemd/network/99-default.link.d/stemdeck-eee.conf"
USB_RULE = "usr/lib/udev/rules.d/90-stemdeck-usb.rules"
EEE_RULE = "usr/lib/udev/rules.d/80-stemdeck-eee-off.rules"
RATE_DROP_IN = "usr/share/stemdeck/10-stemdeck-rate.conf"
A3_CORE_RATE = (REPO / "third_party/a3-core/platform-config/debian-x86_64/a3-core/home/aaa/.local/share"
                "/a3-core/config/pipewire/pipewire.conf.d/10-a3-rate.conf")


def clock_settings(text):
    """The default.clock.* keys of a PipeWire config, comments ignored."""
    found = {}
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if line.startswith("default.clock."):
            key, _, value = line.partition("=")
            found[key.strip()] = value.strip()
    return found


def fields(text):
    found = {}
    for line in text.splitlines():
        if line and not line[0].isspace() and ":" in line:
            key, _, value = line.partition(":")
            found[key] = value.strip()
    return found


def code_lines(path):
    """The lines that run: no comments, no echo (a printed hint is not an action)."""
    return [line for line in path.read_text().splitlines()
            if line.strip() and not line.strip().startswith(("#", "echo"))]


class TheControlFile(unittest.TestCase):
    def setUp(self):
        self.text = (DEBIAN / "control").read_text()
        self.fields = fields(self.text)

    def test_name_architecture_and_a_version_to_stamp(self):
        self.assertEqual("stemdeck", self.fields["Package"])
        self.assertEqual("amd64", self.fields["Architecture"])
        self.assertIn("Version", self.fields)

    def test_depends_are_the_binarys_libraries_and_the_units_tools(self):
        depends = [d.strip() for d in self.fields["Depends"].split(",")]
        self.assertEqual("${shlibs:Depends}", depends[0])
        for package in EXTRA_DEPENDS:
            self.assertIn(package, depends)

    def test_it_depends_on_ethtool_for_the_eee_rule(self):
        depends = [d.strip() for d in self.fields["Depends"].split(",")]
        self.assertIn("ethtool", depends)

    def test_the_binarys_libraries_are_left_to_shlibs(self):
        """Rubber Band's runtime is librubberband3 on forky (the NUCs) and
        librubberband2 on trixie (Raspberry Pi OS): a name written by hand
        holds on one of them only. dpkg-shlibdeps names the right one."""
        by_hand = [d.strip() for d in self.fields["Depends"].split(",")][1:]
        self.assertFalse([d for d in by_hand if d.startswith("lib")], by_hand)

    def test_it_names_no_jack_server_and_no_zita(self):
        for forbidden in ("jackd", "zita"):
            self.assertNotIn(forbidden, self.fields["Depends"])

    def test_it_does_not_need_the_core(self):
        """a3nuc2 runs StemDeck without a Core and must not get REAPER and i3's config."""
        self.assertNotIn("a3-core", self.text)

    def test_it_says_user_data_stays(self):
        self.assertIn("User data stays", self.text)


class TheMaintainerScripts(unittest.TestCase):
    def test_they_are_executable(self):
        for name in ("postinst", "prerm", "postrm"):
            with self.subTest(script=name):
                self.assertTrue(os.access(DEBIAN / name, os.X_OK))

    def test_postinst_enables_for_every_user(self):
        self.assertIn("systemctl --global enable stemdeck.service", (DEBIAN / "postinst").read_text())

    def test_postinst_switches_eee_off_on_the_wired_ports_now(self):
        """No reboot: the udev rule only fires on the next add event. Errors are ignored."""
        lines = [line for line in code_lines(DEBIAN / "postinst") if "ethtool" in line]
        self.assertEqual(1, len(lines), lines)
        self.assertIn("--set-eee", lines[0])
        self.assertIn("eee off", lines[0])
        self.assertIn("|| true", lines[0])

    def test_postinst_writes_into_no_home_and_restarts_nothing(self):
        for line in code_lines(DEBIAN / "postinst"):
            for forbidden in ("restart", "/home", "$HOME", "sudo", "--user enable",
                              "--user start", "cp ", "mv ", "mkdir", "rm ", "chown"):
                with self.subTest(line=line, forbidden=forbidden):
                    self.assertNotIn(forbidden, line)

    def test_prerm_disables_on_remove_and_deconfigure_only(self):
        text = (DEBIAN / "prerm").read_text()
        self.assertIn("systemctl --global disable stemdeck.service", text)
        self.assertIn("remove", text)
        self.assertIn("deconfigure", text)
        for event in ("upgrade", "failed-upgrade"):
            self.assertNotIn(event, "".join(code_lines(DEBIAN / "prerm")))

    def test_prerm_does_nothing_on_upgrade(self):
        """Run with a fake systemctl first on PATH: an upgrade must not call it."""
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "calls"
            fake = Path(tmp) / "systemctl"
            fake.write_text(f'#!/bin/sh\necho "$@" >> {log}\n')
            fake.chmod(0o755)
            env = {**os.environ, "PATH": f"{tmp}:{os.environ['PATH']}"}
            for event, called in (("upgrade", False), ("failed-upgrade", False),
                                  ("remove", True), ("deconfigure", True)):
                log.unlink(missing_ok=True)
                subprocess.run([DEBIAN / "prerm", event], check=True, env=env)
                with self.subTest(event=event):
                    self.assertEqual(called, log.exists())

    def test_prerm_writes_into_no_home_and_restarts_nothing(self):
        for line in code_lines(DEBIAN / "prerm"):
            for forbidden in ("restart", "/home", "$HOME", "sudo", "--user",
                              "cp ", "mv ", "mkdir", "rm ", "chown"):
                with self.subTest(line=line, forbidden=forbidden):
                    self.assertNotIn(forbidden, line)

    def test_postrm_no_longer_disables_and_removes_nothing(self):
        """By postrm the unit file is gone; a disable there leaves dangling links."""
        text = (DEBIAN / "postrm").read_text()
        self.assertNotIn("systemctl", text)
        for line in code_lines(DEBIAN / "postrm"):
            self.assertNotIn("rm ", line)


class TheStagedTree(unittest.TestCase):
    EXPECTED = {
        "usr/bin/stemdeck",
        *(f"usr/lib/stemdeck/{tool}" for tool in TOOLS),
        "usr/share/stemdeck/stemdeck-without-core.xml",
        "usr/lib/systemd/user/stemdeck.service",
        "usr/lib/systemd/user/stemdeck@.service",
        *(f"usr/lib/systemd/user/{unit}" for unit in AES67_UNITS),
        *(f"usr/share/stemdeck/{template}" for template in AES67_TEMPLATES),
        "usr/share/stemdeck/links-aes67.json",
        "usr/share/stemdeck/links-aes67-stereo.json",
        "usr/share/stemdeck/links-core.json",
        RATE_DROP_IN,
        EEE,
        EEE_RULE,
        USB_RULE,
        "usr/share/doc/stemdeck/copyright",
    }

    def stage(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        build = Path(tmp.name) / "build"
        binary = build / "StemDeck_artefacts/Release/StemDeck"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"\x7fELF not really")
        stage = Path(tmp.name) / "stage"
        stage.mkdir()
        subprocess.run([STAGE, "files", REPO, build, stage], check=True)
        return stage

    def files(self, stage):
        return {str(p.relative_to(stage)) for p in stage.rglob("*") if p.is_file()}

    def test_exactly_these_files(self):
        self.assertEqual(self.EXPECTED, self.files(self.stage()))

    def test_programs_are_executable_and_data_is_not(self):
        stage = self.stage()
        for name in self.files(stage):
            mode = (stage / name).stat().st_mode & 0o777
            expected = 0o755 if name.startswith(("usr/bin/", "usr/lib/stemdeck/")) else 0o644
            with self.subTest(file=name):
                self.assertEqual(expected, mode)

    def test_the_unit_is_the_repositorys(self):
        stage = self.stage()
        self.assertEqual(UNIT.read_bytes(),
                         (stage / "usr/lib/systemd/user/stemdeck.service").read_bytes())

    def test_template_is_shipped_not_enabled(self):
        stage = self.stage()
        self.assertEqual(TEMPLATE.read_bytes(),
                         (stage / "usr/lib/systemd/user/stemdeck@.service").read_bytes())
        for name in ("postinst", "prerm", "postrm"):
            with self.subTest(script=name):
                for line in code_lines(DEBIAN / name):
                    self.assertNotIn("stemdeck@", line)

    def test_every_program_the_unit_starts_is_in_the_tree(self):
        stage = self.stage()
        for line in UNIT.read_text().splitlines():
            key, _, value = line.partition("=")
            if not key.startswith("Exec"):
                continue
            for word in value.lstrip("-@:+!").split():
                if word.startswith(("/usr/lib/stemdeck/", "/usr/bin/stemdeck")):
                    with self.subTest(program=word):
                        self.assertTrue((stage / word.lstrip("/")).is_file())

    def test_package_ships_the_deck_sender_unit_and_eee(self):
        stage = self.stage()
        for unit in AES67_UNITS:
            with self.subTest(unit=unit):
                self.assertEqual((UNITS / unit).read_bytes(),
                                 (stage / "usr/lib/systemd/user" / unit).read_bytes())
        for template in AES67_TEMPLATES:
            with self.subTest(template=template):
                self.assertEqual((REPO / "tools" / template).read_bytes(),
                                 (stage / "usr/share/stemdeck" / template).read_bytes())
        eee = (stage / EEE).read_text()
        self.assertIn("[EnergyEfficientEthernet]\nEnable=false\n", eee)

    def test_package_ships_the_eee_off_rule(self):
        """Debian 13's systemd 257 ignores the .link section; udev does the job there."""
        stage = self.stage()
        self.assertEqual((REPO / "packaging/lib/udev/rules.d/80-stemdeck-eee-off.rules").read_bytes(),
                         (stage / EEE_RULE).read_bytes())
        lines = [line for line in (stage / EEE_RULE).read_text().splitlines()
                 if line.strip() and not line.startswith("#")]
        self.assertEqual(1, len(lines), lines)
        rule = lines[0]
        self.assertIn('ACTION=="add"', rule)
        self.assertIn('SUBSYSTEM=="net"', rule)
        self.assertIn('KERNEL=="eth*|en*"', rule)
        self.assertIn('RUN+="/usr/sbin/ethtool --set-eee $name eee off"', rule)

    def test_package_ships_the_usb_rule(self):
        """A USB block device with a filesystem is mounted for the recorder.

        Pinned: the match keys, the mount point, --collect. Nothing here runs
        udev or mount."""
        stage = self.stage()
        rule = stage / USB_RULE
        self.assertEqual((REPO / "packaging/lib/udev/rules.d/90-stemdeck-usb.rules").read_bytes(),
                         rule.read_bytes())
        lines = [line for line in rule.read_text().splitlines()
                 if line.strip() and not line.startswith("#")]
        rules = [line for line in lines if "RUN+=" in line]
        self.assertTrue(rules)
        for line in rules:
            with self.subTest(rule=line):
                self.assertIn('SUBSYSTEM=="block"', line)
                self.assertIn('ENV{ID_BUS}=="usb"', line)
                self.assertIn('ENV{ID_FS_USAGE}=="filesystem"', line)
                self.assertIn("systemd-mount --no-block --automount=yes --collect", line)
                self.assertIn("--timeout-idle-sec=", line)
                self.assertTrue(line.rstrip().endswith('$devnode /media/a3-usb"'), line)
        for line in rules:
            with self.subTest(rule=line):
                self.assertTrue(line.startswith('ACTION=="add", '),
                                "a change event must not mount a mounted stick again")
        owned = [line for line in rules if "uid=1000" in line]
        self.assertEqual(1, len(owned), "one rule owns the files")
        self.assertIn('ENV{ID_FS_TYPE}=="vfat|exfat|ntfs|ntfs3"', owned[0])
        for line in rules:
            if line not in owned:
                self.assertIn('ENV{ID_FS_TYPE}!="vfat|exfat|ntfs|ntfs3"', line)

    def test_usb_rule_leaves_disks_fstab_names(self):
        """A USB disk the machine mounts itself (fstab) is not the recorder's stick."""
        lines = [line for line in (REPO / "packaging/lib/udev/rules.d/90-stemdeck-usb.rules").read_text().splitlines()
                 if line.strip() and not line.startswith("#")]
        skips = [i for i, line in enumerate(lines)
                 if 'PROGRAM=="/usr/bin/findmnt --fstab --source $devnode"' in line
                 and 'GOTO="stemdeck_usb_end"' in line]
        self.assertEqual(1, len(skips))
        mounts = [i for i, line in enumerate(lines) if "RUN+=" in line]
        self.assertLess(skips[0], min(mounts), "the skip comes before every mount")
        self.assertEqual('LABEL="stemdeck_usb_end"', lines[-1])

    def test_package_ships_the_aes67_link_map(self):
        stage = self.stage()
        self.assertEqual((REPO / "data/links-aes67.json").read_bytes(),
                         (stage / "usr/share/stemdeck/links-aes67.json").read_bytes())

    def test_package_ships_the_stereo_link_map_beside_the_aes67_one(self):
        """A stereo instance reads the file beside STEMDECK_LINK_MAP that ends in
        -stereo.json (Source/DeckLinks.cpp): it must be where links-aes67.json is."""
        stage = self.stage()
        self.assertEqual((REPO / "data/links-aes67-stereo.json").read_bytes(),
                         (stage / "usr/share/stemdeck/links-aes67-stereo.json").read_bytes())

    def test_package_ships_the_default_link_map_where_stemdeck_reads_it(self):
        """P39: without STEMDECK_LINK_MAP every StemDeck on the Core links its own
        rec_L/rec_R from the engine's REC bus with this map; the binary names the
        installed path (Source/DeckLinks.h)."""
        stage = self.stage()
        installed = re.search(r'installedCoreMap = "/([^"]+)"', (REPO / "Source/DeckLinks.h").read_text())
        self.assertIsNotNone(installed)
        self.assertEqual((REPO / "data/links-core.json").read_bytes(), (stage / installed.group(1)).read_bytes())
        shipped = json.loads((stage / installed.group(1)).read_text())
        self.assertEqual({"jack": {"rec_L": ["a3-engine:rec_L"], "rec_R": ["a3-engine:rec_R"]}}, shipped)

    def test_package_ships_the_deck_hosts_server_rate(self):
        """The deck-host role copies it into ~/.config/pipewire/pipewire.conf.d/:
        the user's PipeWire server, not the base instance, sets the graph's rate
        and smallest block (P32)."""
        stage = self.stage()
        self.assertEqual((REPO / "data/10-stemdeck-rate.conf").read_bytes(),
                         (stage / RATE_DROP_IN).read_bytes())
        self.assertEqual({
            "default.clock.rate": "44100",
            "default.clock.allowed-rates": "[ 44100 ]",
            "default.clock.quantum": "64",
            "default.clock.min-quantum": "64",
        }, clock_settings((stage / RATE_DROP_IN).read_text()))

    @unittest.skipUnless(A3_CORE_RATE.is_file(), "third_party/a3-core is not checked out")
    def test_server_rate_is_the_cores(self):
        """One rig, one rate: the deck host runs what a3-core sets on the Core."""
        self.assertEqual(clock_settings(A3_CORE_RATE.read_text()),
                         clock_settings((REPO / "data/10-stemdeck-rate.conf").read_text()))

    def test_deck_host_links_aes67_one_two_to_the_recorder(self):
        """The Core's REC bus (CH1/CH2) is the recorder's rec_L/rec_R; StemDeck's
        own linker applies this map, so no second linker lives in the PipeWire config."""
        shipped = json.loads((self.stage() / "usr/share/stemdeck/links-aes67.json").read_text())
        self.assertEqual(["AES67 a3nuc1 out:receive_CH1"], shipped["jack"]["rec_L"])
        self.assertEqual(["AES67 a3nuc1 out:receive_CH2"], shipped["jack"]["rec_R"])

    def test_no_a3_rec_loopback_in_the_package(self):
        """The hand-made a3-rec loopback is replaced by the link map above."""
        stage = self.stage()
        loopback = re.compile(r"\ba3-rec(?![a-z])")
        for path in stage.rglob("*"):
            if not path.is_file():
                continue
            with self.subTest(file=str(path.relative_to(stage))):
                self.assertNotIn("a3-rec", path.name)
                self.assertIsNone(loopback.search(path.read_text(errors="replace")))

    def test_no_aes67_unit_is_enabled_by_the_package(self):
        """The deck-host role pulls them in; a3nuc1 carries the package too."""
        for name in ("postinst", "prerm", "postrm"):
            for line in code_lines(DEBIAN / name):
                for unit in AES67_UNITS:
                    with self.subTest(script=name, unit=unit):
                        self.assertNotIn(unit.split("@")[0], line)

    def test_every_program_the_aes67_units_start_is_in_the_tree(self):
        stage = self.stage()
        for unit in AES67_UNITS:
            for line in (UNITS / unit).read_text().splitlines():
                key, _, value = line.partition("=")
                if not key.startswith("Exec"):
                    continue
                word = value.lstrip("-@:+!").split()[0]
                if word.startswith("/usr/lib/stemdeck/"):
                    with self.subTest(unit=unit, program=word):
                        self.assertTrue((stage / word.lstrip("/")).is_file())

    def test_an_unknown_extra_argument_is_refused(self):
        done = subprocess.run([STAGE, "files", REPO, "b", "s", "extra"], capture_output=True)
        self.assertNotEqual(0, done.returncode)

    def test_an_unknown_step_is_refused(self):
        done = subprocess.run([STAGE, "nonsense"], capture_output=True)
        self.assertEqual(2, done.returncode)


if __name__ == "__main__":
    unittest.main()

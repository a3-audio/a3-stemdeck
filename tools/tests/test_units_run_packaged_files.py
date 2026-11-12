"""StemDeck's units start only what the package ships or the system has.

The rig's unit ran the linker's output inside the checkout, and an in-place
rebuild left it with nothing to run: 203/EXEC, eight times (2026-10-07). The
package puts the binary in /usr/bin and the tools in /usr/lib/stemdeck; no
unit names a checkout any more."""

import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
UNITS = REPO / ".config/systemd/user"
PACKAGED_TOOLS = "/usr/lib/stemdeck/"
SHIPPED_DATA = "/usr/share/stemdeck/"
EXEC_KEYS = ("ExecStartPre", "ExecStart", "ExecStartPost", "ExecStop", "ExecStopPost")


def command_lines(unit):
    """Each Exec line's words, its prefix (-, @, :, +, !) taken off."""
    for line in unit.read_text().splitlines():
        key, _, value = line.partition("=")
        if key in EXEC_KEYS:
            yield value.lstrip("-@:+!").split()


def every_unit():
    return sorted(UNITS.glob("*.service"))


class EveryUnit(unittest.TestCase):
    def test_nothing_runs_from_a_checkout(self):
        for unit in every_unit():
            with self.subTest(unit=unit.name):
                self.assertNotIn("a3-system", unit.read_text())

    def test_every_program_is_the_systems_or_the_packages(self):
        for unit in every_unit():
            for words in command_lines(unit):
                with self.subTest(unit=unit.name, line=words):
                    self.assertTrue(words[0].startswith(("/usr/bin/", PACKAGED_TOOLS)))

    def test_every_packaged_tool_is_in_tools(self):
        for unit in every_unit():
            for words in command_lines(unit):
                for word in words:
                    if word.startswith(PACKAGED_TOOLS):
                        with self.subTest(unit=unit.name, tool=word):
                            self.assertTrue((REPO / "tools" / word[len(PACKAGED_TOOLS):]).is_file())

    def test_every_shipped_file_is_in_the_repository(self):
        for unit in every_unit():
            for words in command_lines(unit):
                for word in words:
                    if word.startswith(SHIPPED_DATA):
                        with self.subTest(unit=unit.name, data=word):
                            self.assertTrue((REPO / ".config/rncbc.org" / word[len(SHIPPED_DATA):]).is_file())


class TheStemDeckUnit(unittest.TestCase):
    UNIT = UNITS / "stemdeck.service"

    def lines(self):
        return self.UNIT.read_text().splitlines()

    def test_it_starts_the_packaged_binary(self):
        self.assertIn("ExecStart=/usr/bin/stemdeck", self.lines())

    def test_it_runs_in_its_data_folder(self):
        """'-': on the very first start the folder is made by the seed."""
        self.assertIn("WorkingDirectory=-%h/.local/share/stemdeck", self.lines())

    def test_the_seed_runs_first_and_cannot_stop_the_start(self):
        pre = [line for line in self.lines() if line.startswith("ExecStartPre=")]
        self.assertEqual("ExecStartPre=-/usr/lib/stemdeck/stemdeck-seed", pre[0])

    def test_it_comes_back_after_a_crash(self):
        self.assertIn("Restart=on-failure", self.lines())


class TheTemplateUnit(unittest.TestCase):
    """stemdeck@.service: one one-deck instance per letter (S4.8)."""
    TEMPLATE = UNITS / "stemdeck@.service"
    TWO_DECK = UNITS / "stemdeck.service"

    def lines(self, unit):
        return unit.read_text().splitlines()

    def pre(self, unit):
        return [l for l in self.lines(unit) if l.startswith("ExecStartPre=")]

    def test_template_unit_starts_one_deck(self):
        self.assertIn("ExecStart=/usr/bin/stemdeck -bformat --deck %i $STEMDECK_WINDOW",
                      self.lines(self.TEMPLATE))
        self.assertEqual(self.pre(self.TWO_DECK), self.pre(self.TEMPLATE))

    def test_the_window_flag_is_one_variable_defaulting_to_fullscreen(self):
        lines = self.lines(self.TEMPLATE)
        self.assertIn("Environment=STEMDECK_WINDOW=--fullscreen", lines)
        # Unbraced: systemd drops an empty unbraced variable, so a laptop
        # drop-in with STEMDECK_WINDOW= passes no argument at all.
        self.assertIn("ExecStart=/usr/bin/stemdeck -bformat --deck %i $STEMDECK_WINDOW", lines)
        self.assertNotIn("--fullscreen", "\n".join(l for l in lines if l.startswith("ExecStart=")))

    def test_template_does_not_conflict_with_the_two_deck_unit(self):
        # 2026-10-27 rig: a3-main pulled stemdeck@A in REAPER mode; the conflict
        # dropped stemdeck.service's start job at boot, then @A was skipped by its
        # condition, and no StemDeck ran. Exclusivity is a3-core's drop-in on
        # the two-deck unit (ExecCondition=/bin/false); this template runs unconditioned.
        self.assertFalse([l for l in self.lines(self.TEMPLATE) if l.startswith("Conflicts=")])

    def test_template_is_never_enabled_by_itself(self):
        self.assertNotIn("[Install]", self.TEMPLATE.read_text())

    def test_template_keeps_the_two_deck_unit_s_environment(self):
        for line in ("Environment=DISPLAY=:0", "WorkingDirectory=-%h/.local/share/stemdeck",
                     "Restart=on-failure", "RestartSec=5",
                     "ExecStartPost=/usr/lib/stemdeck/rig-keep-the-screen.sh restore"):
            with self.subTest(line=line):
                self.assertIn(line, self.lines(self.TEMPLATE))

    def test_a_refused_letter_does_not_restart_forever(self):
        """Exit 2 is a usage error (refused flags), not a transient failure."""
        self.assertIn("RestartPreventExitStatus=2", self.lines(self.TEMPLATE))

    def test_a_change_of_output_mode_restarts_the_instance(self):
        """After storing a new outputMode the app exits with 75 (EX_TEMPFAIL,
        Source/OneDeckComponent.cpp). A non-zero status is a failure to
        Restart=on-failure, so the unit needs no change for it -- as long as
        75 is not one of the statuses that prevent a restart."""
        lines = self.lines(self.TEMPLATE)
        self.assertIn("Restart=on-failure", lines)
        self.assertIn("RestartPreventExitStatus=2", lines)
        source = (REPO / "Source/OneDeckComponent.cpp").read_text()
        self.assertIn("restartExitStatus = 75", source)

    def test_the_restart_renders_the_sender_again_for_the_new_mode(self):
        """A deck host's AES67 sender is rendered at its own start from the
        instance's outputMode: a started StemDeck restarts it if it runs
        (try-restart: a Core, which has none, is untouched)."""
        self.assertIn("ExecStartPost=-/usr/bin/systemctl --user try-restart a3-aes67-deck@%i.service",
                      self.lines(self.TEMPLATE))

    def test_two_deck_unit_unchanged(self):
        done = subprocess.run(
            ["git", "show", "main:.config/systemd/user/stemdeck.service"],
            cwd=REPO, capture_output=True)
        if done.returncode != 0:
            self.skipTest("no local main ref to compare with (tarball or detached checkout)")
        committed = done.stdout
        self.assertEqual(committed, self.TWO_DECK.read_bytes())


class TheDeckSenderUnit(unittest.TestCase):
    """a3-aes67-deck@.service: deck %i's AES67 sender on a deck host (R4)."""
    UNIT = UNITS / "a3-aes67-deck@.service"

    def lines(self):
        return self.UNIT.read_text().splitlines()

    def test_unit_runs_packaged_files(self):
        for line in ("Description=AES67 sender of deck %i",
                     "ExecStartPre=/usr/lib/stemdeck/stemdeck-aes67-deck render %i",
                     "ExecStart=/usr/bin/pipewire -c %h/.config/pipewire/pipewire-aes67-deck-%i.conf",
                     "Restart=always",
                     "RestartSec=2"):
            with self.subTest(line=line):
                self.assertIn(line, self.lines())
        self.assertTrue((REPO / "tools/stemdeck-aes67-deck").is_file())

    def test_it_joins_the_base_instance_s_clock(self):
        """Its rtp-sink joins pipewire.ptp0, which the base's PTP0-Driver drives."""
        self.assertIn("Requires=stemdeck-aes67.service", self.lines())
        self.assertIn("After=stemdeck-aes67.service", self.lines())

    def test_no_conflicts_and_no_install(self):
        self.assertFalse([l for l in self.lines() if l.startswith("Conflicts=")])
        self.assertNotIn("[Install]", self.UNIT.read_text())

    def test_it_never_runs_beside_a3_cores_aes67(self):
        """A condition-skipped base still satisfies Requires=: the sender checks itself."""
        lines = self.lines()
        condition = "ExecCondition=/usr/lib/stemdeck/stemdeck-aes67-deck base-wanted"
        self.assertIn(condition, lines)
        self.assertLess(lines.index(condition), lines.index(
            "ExecStartPre=/usr/lib/stemdeck/stemdeck-aes67-deck render %i"))


class TheBaseUnit(unittest.TestCase):
    """stemdeck-aes67.service: the deck host's PTP clock and receiver (P10, P24)."""
    UNIT = UNITS / "stemdeck-aes67.service"

    def lines(self):
        return self.UNIT.read_text().splitlines()

    def test_unit_runs_packaged_files(self):
        for line in ("ExecCondition=/usr/lib/stemdeck/stemdeck-aes67-deck base-wanted",
                     "ExecStartPre=/usr/lib/stemdeck/stemdeck-aes67-deck render-base",
                     "ExecStart=/usr/bin/pipewire -c %h/.config/pipewire/pipewire-stemdeck-aes67.conf",
                     "Restart=always",
                     "RestartSec=2"):
            with self.subTest(line=line):
                self.assertIn(line, self.lines())

    def test_the_condition_runs_before_the_render(self):
        lines = self.lines()
        condition = lines.index("ExecCondition=/usr/lib/stemdeck/stemdeck-aes67-deck base-wanted")
        render = lines.index("ExecStartPre=/usr/lib/stemdeck/stemdeck-aes67-deck render-base")
        self.assertLess(condition, render)

    def test_it_runs_on_the_audio_server(self):
        self.assertIn("Requires=pipewire.service", self.lines())
        self.assertIn("After=pipewire.service wireplumber.service", self.lines())

    def test_no_conflicts_and_no_install(self):
        """Chosen by ExecCondition=, never by Conflicts= (2026-10-27 boot trap)."""
        self.assertFalse([l for l in self.lines() if l.startswith("Conflicts=")])
        self.assertNotIn("[Install]", self.UNIT.read_text())

    def test_it_is_not_a3_cores_unit(self):
        self.assertFalse((UNITS / "a3-aes67.service").exists())


class TheAes67Units(unittest.TestCase):
    UNITS = ("a3-aes67-deck@.service", "stemdeck-aes67.service")

    CONDITIONS = {
        "a3-aes67-deck@.service": ("base-wanted", "can-render %i"),
        "stemdeck-aes67.service": ("base-wanted", "can-render-base"),
    }

    def test_a_render_that_would_refuse_skips_the_unit(self):
        """A failed ExecStartPre is restarted whatever RestartPreventExitStatus
        says (measured, systemd 262): the refusal is a condition instead."""
        for unit, steps in self.CONDITIONS.items():
            lines = (UNITS / unit).read_text().splitlines()
            first_pre = min(i for i, l in enumerate(lines) if l.startswith("ExecStartPre="))
            for step in steps:
                line = f"ExecCondition=/usr/lib/stemdeck/stemdeck-aes67-deck {step}"
                with self.subTest(unit=unit, step=step):
                    self.assertIn(line, lines)
                    self.assertLess(lines.index(line), first_pre)

    def test_no_restart_prevention_that_does_nothing(self):
        for unit in self.UNITS:
            with self.subTest(unit=unit):
                self.assertNotIn("RestartPreventExitStatus", (UNITS / unit).read_text())

    def test_they_keep_the_host_s_audio_port(self):
        """The deck-host role sets it (P28); a unit that set it would override the role."""
        for unit in self.UNITS:
            with self.subTest(unit=unit):
                self.assertNotIn("Environment=STEMDECK_AUDIO_PORT",
                                 (UNITS / unit).read_text())
                self.assertNotIn("UnsetEnvironment", (UNITS / unit).read_text())

    def test_they_carry_no_license_header(self):
        """.reuse/dep5 licenses .config/*, as for the repository's other units."""
        for unit in self.UNITS:
            with self.subTest(unit=unit):
                self.assertNotIn("SPDX", (UNITS / unit).read_text())


if __name__ == "__main__":
    unittest.main()

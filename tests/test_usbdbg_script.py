"""Run payload/diskos-debug.sh usb-on / usb-off / usb-status against a fake configfs, UDC and role tree."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'payload' / 'diskos-debug.sh'
BOOT_HOOK = Path(__file__).resolve().parents[1] / 'payload' / 'S99usbserial'

SHIMS = {
    'rmdir': '#!/bin/sh\nexec rm -rf "$@"\n',
    'sleep': '#!/bin/sh\nexec /bin/sleep 0.02\n',
    'fuser': '#!/bin/sh\necho "fuser $*" >> "$SHIMLOG"\nexit 1\n',
    'mount': ('#!/bin/sh\n'
              'if [ "$1" = -t ] && [ "$2" = configfs ]; then\n'
              '  echo "mount $*" >> "$SHIMLOG"; mkdir -p "$4/usb_gadget"; exit 0\n'
              'fi\n'
              'exit 1\n'),
    'umount': '#!/bin/sh\necho "umount $*" >> "$SHIMLOG"\nrm -rf "$1/usb_gadget"\nexit 0\n',
}


@unittest.skipIf(os.name == 'nt', 'needs a POSIX shell')
class UsbDebugScriptTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='diskos-usbdbg-')
        self.root = Path(self.tmp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        for name, body in SHIMS.items():
            p = self.bin / name
            p.write_text(body)
            p.chmod(0o755)
        self.cfg = self.root / 'config'
        self.cfg.mkdir()
        self.udc = self.root / 'udc'
        (self.udc / '13500000.otg_new').mkdir(parents=True)
        (self.udc / '13500000.otg_new' / 'state').write_text('not attached\n')
        self.role = self.root / 'role'
        self.role.write_text('none\n')
        self.self_dir = self.root / 'sshd'
        self.self_dir.mkdir()
        self.run_dir = self.root / 'run'
        self.log = self.root / 'shim.log'
        self.log.write_text('')
        self.env = dict(os.environ,
                        PATH=f'{self.bin}:{os.environ["PATH"]}',
                        SHIMLOG=str(self.log),
                        DISKOS_SELF=str(self.self_dir),
                        DISKOS_CONFIGFS=str(self.cfg),
                        DISKOS_UDC_DIR=str(self.udc),
                        DISKOS_USB_ROLE=str(self.role),
                        DISKOS_TTY_SHELL='/dev/null',
                        DISKOS_USBDBG_RUN=str(self.run_dir))

    def tearDown(self):
        self.sh('usb-off')
        pid = self.self_dir / 'serial.pid'
        if pid.exists():
            try:
                os.kill(int(pid.read_text().strip()), 9)
            except (OSError, ValueError):
                pass
        self.tmp.cleanup()

    def sh(self, *args):
        return subprocess.run(['sh', str(SCRIPT), *args], env=self.env, capture_output=True, text=True, timeout=60)

    @property
    def gadget(self):
        return self.cfg / 'usb_gadget' / 'diskos_dbg'

    def shim_calls(self):
        return self.log.read_text().splitlines()

    def test_usb_on_builds_one_acm_gadget_and_binds_the_udc(self):
        r = self.sh('usb-on')
        self.assertEqual((r.returncode, r.stdout.strip()), (0, 'on shell=on'), r.stderr)
        g = self.gadget
        self.assertEqual((g / 'UDC').read_text().strip(), '13500000.otg_new')
        self.assertEqual((g / 'idVendor').read_text().strip(), '0x0525')
        self.assertEqual((g / 'idProduct').read_text().strip(), '0xa4a7')
        self.assertEqual((g / 'bDeviceClass').read_text().strip(), '0x02')
        self.assertEqual(sorted(p.name for p in (g / 'functions').iterdir()), ['acm.0'])
        self.assertEqual(sorted(p.name for p in (g / 'configs/c.1').iterdir() if p.is_symlink()), ['acm.0'])
        self.assertEqual(os.readlink(g / 'configs/c.1/acm.0'), str(g / 'functions/acm.0'))

    def test_usb_on_never_adds_a_network_function(self):
        self.sh('usb-on')
        names = sorted(p.name for p in (self.gadget / 'functions').iterdir())
        for bad in ('ecm', 'rndis', 'ncm', 'eem', 'geth', 'subset'):
            self.assertFalse(any(n.startswith(bad) for n in names), names)

    def test_usb_on_mounts_configfs_only_when_missing_and_usb_off_unmounts_it(self):
        self.assertFalse((self.cfg / 'usb_gadget').exists())
        self.assertEqual(self.sh('usb-on').returncode, 0)
        self.assertEqual([c for c in self.shim_calls() if c.startswith('mount')], [f'mount -t configfs none {self.cfg}'])
        self.assertEqual(self.sh('usb-off').stdout.strip(), 'off')
        self.assertEqual([c for c in self.shim_calls() if c.startswith('umount')], [f'umount {self.cfg}'])

    def test_usb_off_leaves_a_configfs_it_did_not_mount(self):
        (self.cfg / 'usb_gadget').mkdir()
        self.assertEqual(self.sh('usb-on').returncode, 0)
        self.assertEqual(self.sh('usb-off').stdout.strip(), 'off')
        self.assertEqual([c for c in self.shim_calls() if c.startswith(('mount', 'umount'))], [])
        self.assertTrue((self.cfg / 'usb_gadget').is_dir())

    def test_usb_off_removes_the_gadget_and_stops_the_shell(self):
        self.sh('usb-on')
        pid = int((self.self_dir / 'serial.pid').read_text())
        os.kill(pid, 0)
        r = self.sh('usb-off')
        self.assertEqual((r.returncode, r.stdout.strip()), (0, 'off'), r.stderr)
        self.assertFalse(self.gadget.exists())
        with self.assertRaises(OSError):
            os.kill(pid, 0)
        self.assertTrue((self.udc / '13500000.otg_new').is_dir())

    def test_usb_on_twice_keeps_one_shell_supervisor(self):
        self.sh('usb-on')
        first = (self.self_dir / 'serial.pid').read_text()
        r = self.sh('usb-on')
        self.assertEqual((r.returncode, r.stdout.strip()), (0, 'on shell=on'))
        self.assertEqual((self.self_dir / 'serial.pid').read_text(), first)

    def test_usb_off_with_nothing_active_is_a_clean_no_op(self):
        r = self.sh('usb-off')
        self.assertEqual((r.returncode, r.stdout.strip()), (0, 'off'), r.stderr)

    def test_stock_usb_dac_gadget_makes_usb_on_refuse_without_touching_it(self):
        stock = self.cfg / 'usb_gadget' / 'uac_demo'
        stock.mkdir(parents=True)
        (stock / 'UDC').write_text('13500000.otg_new\n')
        r = self.sh('usb-on')
        self.assertEqual(r.returncode, 2)
        self.assertIn('another USB gadget', r.stdout)
        self.assertFalse(self.gadget.exists())
        self.assertEqual((stock / 'UDC').read_text().strip(), '13500000.otg_new')

    def test_stock_storage_gadget_makes_usb_on_refuse(self):
        (self.cfg / 'usb_gadget' / 'storage_demo').mkdir(parents=True)
        self.assertEqual(self.sh('usb-on').returncode, 2)
        self.assertFalse(self.gadget.exists())

    def test_no_udc_fails_cleanly_and_leaves_nothing_behind(self):
        shutil.rmtree(self.udc / '13500000.otg_new')
        r = self.sh('usb-on')
        self.assertEqual(r.returncode, 3, r.stdout)
        self.assertFalse(self.gadget.exists())
        self.assertEqual([c for c in self.shim_calls() if c.startswith('umount')], [f'umount {self.cfg}'])
        self.assertFalse((self.self_dir / 'serial.pid').exists())

    def test_role_is_set_to_device_and_given_back(self):
        self.sh('usb-on')
        self.assertEqual(self.role.read_text().strip(), 'device')
        self.sh('usb-off')
        self.assertEqual(self.role.read_text().strip(), 'none')

    def test_role_already_device_is_left_alone(self):
        self.role.write_text('device\n')
        self.sh('usb-on')
        self.sh('usb-off')
        self.assertEqual(self.role.read_text().strip(), 'device')

    def test_usb_status_reports_state_udc_host_and_shell(self):
        self.assertEqual(self.sh('usb-status').stdout.strip(), 'USBDBG=off UDC=13500000.otg_new HOST=not_attached SHELL=off')
        self.sh('usb-on')
        (self.udc / '13500000.otg_new' / 'state').write_text('configured\n')
        self.assertEqual(self.sh('usb-status').stdout.strip(), 'USBDBG=on UDC=13500000.otg_new HOST=configured SHELL=on')

    def test_serial_off_does_not_cut_the_shell_while_usb_debug_owns_it(self):
        self.sh('usb-on')
        r = self.sh('serial-off')
        self.assertIn('USB Debug', r.stdout)
        self.assertIn('SERIAL=on', self.sh('status').stdout)

    def test_legacy_serial_demo_gadget_is_replaced(self):
        legacy = self.cfg / 'usb_gadget' / 'serial_demo'
        (legacy / 'functions' / 'acm.0').mkdir(parents=True)
        (legacy / 'configs' / 'c.1').mkdir(parents=True)
        (legacy / 'UDC').write_text('13500000.otg_new\n')
        r = self.sh('usb-on')
        self.assertEqual((r.returncode, r.stdout.strip()), (0, 'on shell=on'), r.stderr)
        self.assertFalse(legacy.exists())
        self.assertEqual((self.gadget / 'UDC').read_text().strip(), '13500000.otg_new')

    def test_unknown_verb_prints_usage_with_the_usb_verbs(self):
        r = self.sh('nonsense')
        self.assertEqual(r.returncode, 1)
        for verb in ('usb-on', 'usb-off', 'usb-status'):
            self.assertIn(verb, r.stdout)


@unittest.skipIf(os.name == 'nt', 'needs a POSIX shell')
class BootHookTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='diskos-s99-')
        self.root = Path(self.tmp.name)
        self.calls = self.root / 'calls'
        self.log = self.root / 'usbserial.log'
        self.fake = self.root / 'diskos-debug.sh'

    def tearDown(self):
        self.tmp.cleanup()

    def fake_script(self, codes):
        self.fake.write_text('#!/bin/sh\necho "$1" >> "%s"\nn=$(wc -l < "%s")\nset -- %s\nshift $((n-1))\necho "result $1"\nexit $1\n'
                             % (self.calls, self.calls, ' '.join(str(c) for c in codes)))
        self.fake.chmod(0o755)

    def start(self):
        env = dict(os.environ, DISKOS_DEBUG_SCRIPT=str(self.fake), DISKOS_USBSERIAL_LOG=str(self.log),
                   DISKOS_USBSERIAL_PAUSE='0', DISKOS_USBSERIAL_TRIES='3')
        r = subprocess.run(['sh', str(BOOT_HOOK), 'start'], env=env, capture_output=True, text=True, timeout=10)
        self.assertEqual(r.returncode, 0)
        for _ in range(100):
            if self.log.exists() and ('usb-on rc=' in self.log.read_text()):
                break
            subprocess.run(['sleep', '0.05'])
        subprocess.run(['sleep', '0.3'])
        return self.calls.read_text().split() if self.calls.exists() else []

    def test_one_success_runs_usb_on_once(self):
        self.fake_script([0])
        self.assertEqual(self.start(), ['usb-on'])
        self.assertIn('usb-on rc=0', self.log.read_text())

    def test_no_udc_is_retried_a_bounded_number_of_times(self):
        self.fake_script([3, 3, 3, 3, 3])
        self.assertEqual(self.start(), ['usb-on'] * 3)

    def test_a_late_udc_succeeds_on_a_retry(self):
        self.fake_script([3, 0])
        self.assertEqual(self.start(), ['usb-on'] * 2)

    def test_a_stock_gadget_is_not_retried(self):
        self.fake_script([2, 0])
        self.assertEqual(self.start(), ['usb-on'])

    def test_other_actions_are_refused(self):
        r = subprocess.run(['sh', str(BOOT_HOOK), 'stop'], capture_output=True, text=True, timeout=10)
        self.assertEqual(r.returncode, 1)

    def test_the_hook_no_longer_binds_the_udc_or_loops(self):
        text = BOOT_HOOK.read_text()
        self.assertNotIn('while true', text)
        self.assertNotIn('/UDC', text)
        self.assertNotIn('serial_demo', text)


if __name__ == '__main__':
    unittest.main()

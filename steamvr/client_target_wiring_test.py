"""Integration guards for host-assisted targeting, including desktop hosts."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parent.parent

class ClientTargetWiring(unittest.TestCase):
    def test_desktop_observes_server_without_enabling_vr_combat(self):
        text = (ROOT / 'vr/vrcam.cpp').read_text()
        desktop = text[text.index('bool StartDesktop('):text.index('} // namespace', text.index('bool StartDesktop('))]
        self.assertIn('melee::InstallTargetReader();', desktop)
        self.assertNotIn('melee::Install();', desktop)
        tick = text[text.index('// Health has its own lifecycle'):text.index('if(pcvrdesktop::Enabled()){')]
        self.assertIn('targetlock::TickNetwork(', tick)
        self.assertIn('targetlock::Stop();', text)

    def test_client_uses_host_snapshot_not_server_dereference(self):
        text = (ROOT / 'vr/target_lock.cpp').read_text()
        self.assertIn('targetreports::Init(ctx, &SamplePlayer)', text)
        self.assertIn('targetreports::Get(', text)
        self.assertIn('clientcombat::NativeOnly(', text)
        self.assertIn('melee::PlayerTargets(', text)

    def test_read_only_observer_does_not_install_combat_hooks(self):
        text = (ROOT / 'vr/melee.cpp').read_text()
        self.assertIn('void InstallTargetReader()', text)
        begin = text.index('void InstallTargetReader()')
        end = text.index('\nbool PlayerTargets(', begin)
        observer = text[begin:end]
        self.assertIn('LookupRva,LookupSig', observer)
        self.assertIn('RelationRva,RelationSig', observer)
        self.assertIn('HashSig', observer)
        for unwanted in ('TargetRva', 'RangeRva', 'FlagsRva', 'SetModeRva', 'InstallSmite', 'InstallStrikes'):
            self.assertNotIn(unwanted, observer)
        sampler = text[end:text.index('\nvoid Install()', end)]
        self.assertIn('framevr::servertarget::Scan(', sampler)
        self.assertNotIn('reinterpret_cast<uintptr_t>(playerHandle)', sampler)

if __name__ == '__main__':
    unittest.main()

"""Runtime desktop wiring contracts; headset/game validation remains separate."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parent.parent

class DesktopRuntimeTests(unittest.TestCase):
    def test_desktop_init_precedes_settings_and_graphics(self):
        text = (ROOT/'vr/vrcam.cpp').read_text()
        body = text.split('D2RL_PLUGIN_EXPORT bool D2RLoaderLoadPlugin(',1)[1]
        self.assertIn('if(pcvrdesktop::Enabled())return StartDesktop(ctx);', body)
        self.assertLess(body.index('return StartDesktop(ctx)'), body.index('vrsettings::Poll(g_iniPath)'))
        self.assertLess(body.index('return StartDesktop(ctx)'), body.index('LoadReShade()'))

    def test_desktop_tick_does_not_publish_tracking(self):
        text = (ROOT/'vr/vrcam.cpp').read_text()
        self.assertIn('void DesktopTick()', text)
        body = text.split('void DesktopTick(){',1)[1].split('bool StartDesktop(',1)[0]
        self.assertIn('upperbody::Tick(world,nullptr,unit)',body)
        self.assertIn('PublishRemoteBodies(world)',body)
        for forbidden in ['HookXInput(', 'OpenShared(', 'LoadReShade(', 'PushArms(', 'MouseTick(']:
            self.assertNotIn(forbidden,body)

if __name__ == '__main__': unittest.main()

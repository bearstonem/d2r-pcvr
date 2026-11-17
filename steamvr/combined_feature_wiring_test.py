"""Regression checks for the three-feature 0.3.33 integration seams."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CombinedWiring(unittest.TestCase):
    def test_health_initializes_before_desktop_return(self):
        text = (ROOT / 'vr/vrcam.cpp').read_text()
        self.assertLess(text.index('partyhealthreports::Init(ctx,&Log);'),
                        text.index('if(pcvrdesktop::Enabled())return StartDesktop(ctx);'))

    def test_health_ticks_before_desktop_continue(self):
        text = (ROOT / 'vr/vrcam.cpp').read_text()
        update = text[text.index('DWORD WINAPI UpdateThread'):]
        self.assertLess(update.index('partyhealthreports::Tick('),
                        update.index('if(pcvrdesktop::Enabled()){'))

    def test_single_shutdown_and_companion_submission(self):
        text = (ROOT / 'steamvr/transport.cpp').read_text()
        shutdown = text[text.index('void Shutdown(bool preserveResolution)'):]
        self.assertEqual(shutdown.count('EndPair();ClearInput();'), 1)
        self.assertEqual(shutdown.count('allyPainter.Stop();'), 1)
        self.assertEqual(text.count('motionQuad(companionRow,'), 1)
        self.assertIn('NavigationCompanionsPanel(interactionInput.Wrist(),', text)
        self.assertNotIn('NavigationMapPanel(interactionInput.hand[1],', text)

    def test_upstream_map_zoom_unchanged(self):
        baseline = ROOT.parent / 'baseline'
        if not baseline.exists():
            self.skipTest('local pristine baseline not present')
        for name in ('map_zoom.h', 'map_zoom_test.cpp', 'wrist_full_map.h', 'ui_blit.cpp'):
            self.assertEqual((ROOT / 'steamvr' / name).read_bytes(),
                             (baseline / 'steamvr' / name).read_bytes(), name)

if __name__ == '__main__':
    unittest.main()

"""Start the assistant from source, a full package or a small standalone kit."""
import os
from pathlib import Path
import runpy
import sys


def main():
    here = Path(__file__).resolve().parent
    # The embeddable interpreter uses an isolated ._pth, so it does not add
    # this standalone kit's script directory automatically.
    sys.path.insert(0, str(here))
    if not (here/'game_install.py').is_file():
        installed = Path(os.environ.get('LOCALAPPDATA', ''))/'Programs/D2R PCVR'
        helpers = installed/'steamvr'
        if not (helpers/'setup_windows.py').is_file():
            raise SystemExit('Install Unleashed with the official Windows installer first, then run Co-op Setup again.')
        sys.path.insert(1, str(helpers))
        from setup_windows import verify_release
        verify_release(installed)
    runpy.run_path(str(here/'coop_setup.py'), run_name='__main__')


if __name__ == '__main__':
    main()

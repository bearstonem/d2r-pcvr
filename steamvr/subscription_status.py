"""Print enemy/object room coverage from the selected profile's shared status."""
import argparse
import json
from pathlib import Path
from render_distance_status import read_status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ini', type=Path, required=True,
                        help='Path to the running profile\'s d2r_pcvr.ini')
    args = parser.parse_args()
    status = read_status(args.ini)
    if 'subscription_requested_rings' not in status:
        raise SystemExit('This running DLL does not publish enemy/object coverage status.')
    print(json.dumps(status, indent=2))


if __name__ == '__main__':
    main()

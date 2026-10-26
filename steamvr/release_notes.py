"""Generate the in-headset summary from RELEASE_NOTES.md and validate packages."""
import argparse
import hashlib
import json
from pathlib import Path
import re

PROJECT_URL = 'https://github.com/bearstonem/d2r-pcvr'
AUTHOR = 'A mod by monkeylashes'
START, END = '<!-- PCVR_IN_HEADSET_NOTES', 'PCVR_IN_HEADSET_NOTES -->'
# The headset's Notes tab pages them, 12 a page (vr_settings.h NotesPerPage,
# NotesMaxLines): a release after a long gap can list everything.
MAX_LINES = 72


def read_notes(notes, identity):
    text = notes.read_text(encoding='utf-8-sig')
    heading = re.match(r'# D2R VR Unleashed v(\d+\.\d+\.\d+) - Windows preview\s*\n', text)
    plugin = re.search(r'^#define D2RPCVR_VERSION "([0-9.]+)"$',
                       identity.read_text(encoding='utf-8-sig'), re.MULTILINE)
    if not heading or not plugin or heading[1] != plugin[1]:
        raise ValueError('RELEASE_NOTES.md heading must match D2RPCVR_VERSION before building.')
    if text.count(START) != 1 or text.count(END) != 1 or text.index(END) < text.index(START):
        raise ValueError('RELEASE_NOTES.md needs exactly one PCVR_IN_HEADSET_NOTES block.')
    rows = text.split(START, 1)[1].split(END, 1)[0].strip().splitlines()
    version = heading[1]
    tag = f'v{version}-windows-preview'
    if len(rows) < 3 or rows[0] != 'release: ' + tag or not rows[1].startswith('credits: '):
        raise ValueError('Update the in-headset summary release tag and credits for this release.')
    lines = rows[2:]
    if not 1 <= len(lines) <= MAX_LINES or any(not line.startswith('- ') for line in lines):
        raise ValueError(f'The in-headset summary needs 1-{MAX_LINES} one-line feature bullets.')
    lines = [line[2:].strip() for line in lines]
    credits = rows[1][len('credits: '):].strip()
    if any('{' in line or '}' in line for line in [credits, *lines]):
        raise ValueError('Fill the release template placeholders in the in-headset summary.')
    if any(not line or len(line) > 100 or any(ord(c) < 32 for c in line)
           for line in [credits, *lines]):
        raise ValueError('Keep each in-headset line nonempty and within 100 characters.')
    return dict(version=version, tag=tag, title=f'D2R VR Unleashed / v{version}',
                author=AUTHOR, project_url=PROJECT_URL,
                release_url=PROJECT_URL + '/releases/tag/' + tag, credits=credits, lines=lines)


def marker(notes):
    payload = json.dumps(notes, sort_keys=True, ensure_ascii=True).encode('utf-8')
    return 'D2RPCVR_RELEASE_NOTES:' + hashlib.sha256(payload).hexdigest() + ';'


def header(notes):
    fields = {'ReleaseVersion': notes['version'], 'ReleaseTitle': notes['title'],
              'ReleaseTag': notes['tag'], 'ReleaseUrl': notes['release_url'],
              'ProjectUrl': notes['project_url'], 'Author': notes['author'],
              'Credits': notes['credits'], 'ContentMarker': marker(notes)}
    result = '// Generated from RELEASE_NOTES.md; do not edit.\n#pragma once\n#include <array>\n'
    result += 'namespace framevr::releasenotes {\n'
    for key, value in fields.items():
        result += f'inline constexpr const char* {key}={json.dumps(value)};\n'
    result += f'inline constexpr std::array<const char*,{len(notes["lines"])}> ReleaseLines{{{{\n'
    result += ',\n'.join('    ' + json.dumps(line) for line in notes['lines'])
    return result + '\n}};\n}\n'


def validate_package(notes, release_tag, plugin):
    if release_tag is not None and release_tag != notes['tag']:
        raise ValueError('Package release tag does not match the plugin version and RELEASE_NOTES.md.')
    if marker(notes).encode('ascii') + b'\0' not in plugin.read_bytes():
        raise ValueError('DLL release notes are missing or stale. Rebuild the plugin from the updated release notes.')
    return {**notes, 'content_marker': marker(notes)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--notes', type=Path, required=True)
    parser.add_argument('--identity', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        content = header(read_notes(args.notes, args.identity))
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding='utf-8') != content:
        args.output.write_text(content, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()

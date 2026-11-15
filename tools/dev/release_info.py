"""Prints the release of the version set in CMakeLists.txt, for CI
(.github/workflows/cmake-psvita.yml) and for checking by hand:

    python3 tools/dev/release_info.py [--repo owner/name] [--notes FILE]

Output, one KEY=value per line (shell-sourceable): TAG, TITLE, PRERELEASE.
The notes are the version's section of CHANGELOG.md, with the links to the
repository's files made absolute (pinned to the tag), written to FILE
(default: release-notes.md). Fails if CHANGELOG.md has no section for the
version, so a version is never released undocumented."""
import argparse
import os
import re
import shlex
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')


def cmake_vars():
    text = open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    out = {}
    for key in ('TITLE', 'TITLE_ID', 'VERSION_MAJOR', 'VERSION_MINOR', 'VERSION_PATCH',
                'VERSION_LABEL', 'VERSION_NAME'):
        m = re.search(r'^set\(%s\s+"([^"]*)"\)' % key, text, re.M)
        if not m:
            sys.exit('CMakeLists.txt: %s not found' % key)
        out[key] = m.group(1)
    return out


def changelog_section(version):
    text = open(os.path.join(ROOT, 'CHANGELOG.md'), encoding='utf-8').read()
    m = re.search(r'^## %s(?=[ \t]|$)[^\n]*\n(.*?)(?=^## |\Z)' % re.escape(version), text, re.M | re.S)
    if not m:
        sys.exit('CHANGELOG.md: no section "## %s"' % version)
    return m.group(1).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='LouisF37/lune-vita')
    ap.add_argument('--notes', default='release-notes.md')
    args = ap.parse_args()

    v = cmake_vars()
    version = '%s.%s.%s' % (v['VERSION_MAJOR'], v['VERSION_MINOR'], v['VERSION_PATCH'])
    if v['VERSION_LABEL']:
        version += '-' + v['VERSION_LABEL']
    tag = 'v' + version
    title = '%s %s "%s"' % (v['TITLE'], version, v['VERSION_NAME'])

    base = 'https://github.com/%s/blob/%s/' % (args.repo, tag)
    section = changelog_section(version)
    # Relative links to files of the repository, e.g. (docs/x.md), become absolute.
    section = re.sub(r'\]\((?!https?://|#)([^)]+)\)', lambda m: '](' + base + m.group(1) + ')', section)
    intro = ('Install `%s-%s.vpk` with VitaShell. %s has its own title ID (`%s`): it installs next to '
             'Moonlight without changing it.\n\n' % (v['TITLE'].lower(), tag, v['TITLE'], v['TITLE_ID']))
    with open(args.notes, 'w', encoding='utf-8') as f:
        f.write(intro + section + '\n')

    print('TAG=%s' % shlex.quote(tag))
    print('TITLE=%s' % shlex.quote(title))
    print('PRERELEASE=%d' % (1 if v['VERSION_LABEL'] else 0))


if __name__ == '__main__':
    main()

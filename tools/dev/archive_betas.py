"""Keeps the Releases page to final versions and the development build, without
losing anything: the pre-releases of a version (their VPK, notes and GitHub data)
go into one zip attached to that version's final release.

    python3 tools/dev/archive_betas.py --final v1.0.0 --betas v1.0.0-beta. [--also build-dev] [--apply]
    python3 tools/dev/archive_betas.py --final v1.6.0 --from-dev-build --apply      # what CI runs

Sources:
  --betas PREFIX    releases whose tag starts with PREFIX (the betas published as GitHub
                    releases until 22 November 2026). Each one's assets, notes and release
                    data go into the zip; then the release is deleted and its git tag kept.
  --from-dev-build  the VPKs of pre-release versions that CI parks on the development build
                    (build-main), named vitamoon-v<version>-<label>.vpk: moved into the zip,
                    then removed from the development build. Their notes are the version's
                    section of CHANGELOG.md; their tags were made by CI.
  --also TAG        another release, kept whole under builds/ in the zip; then the release
                    and its tag are deleted (for rolling builds of removed branches).

The zip is vitamoon-<final>-betas.zip. If the final release already has one, its content is
kept and the new entries added. Nothing is deleted before the zip has been uploaded, read back
and checked file by file (SHA-256). Without --apply, nothing changes on GitHub (dry run). Needs
the GitHub CLI (`gh`), signed in, or GH_TOKEN in CI.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
DEV_BUILD = 'build-main'
BETA_VPK = re.compile(r'^vitamoon-v(\d+\.\d+\.\d+-[0-9A-Za-z.]+)\.vpk$')
NOTE_MARKER = '<!-- vitamoon-betas-archive -->'


def gh(*args, binary=False):
    out = subprocess.run(['gh'] + list(args), capture_output=True)
    if out.returncode != 0:
        sys.exit('gh %s: %s' % (' '.join(args), out.stderr.decode('utf-8', 'replace').strip()))
    return out.stdout if binary else out.stdout.decode('utf-8')


def api(repo, path, method='GET', fields=None):
    args = ['api', '-X', method, 'repos/%s/%s' % (repo, path)]
    for k, v in (fields or {}).items():
        args += ['-f', '%s=%s' % (k, v)]
    out = gh(*args)
    return json.loads(out) if out.strip() else None


def upload_asset(repo, release, path):
    """Uploads through the REST API (works where gh's GraphQL calls are not allowed),
    under a temporary name; returns the new asset. An asset already there keeps its name
    until replace_asset() is called."""
    name = os.path.basename(path) + '.new'
    for a in api(repo, 'releases/%d' % release['id'])['assets']:
        if a['name'] == name:   # left by an interrupted run
            api(repo, 'releases/assets/%d' % a['id'], 'DELETE')
    out = gh('api', '-X', 'POST', '-H', 'Content-Type: application/zip', '--input', path,
             'https://uploads.github.com/repos/%s/releases/%d/assets?name=%s' % (repo, release['id'], name))
    return json.loads(out)


def replace_asset(repo, release, new_asset, name):
    """The checked upload takes the final name; the asset it replaces is deleted."""
    for a in api(repo, 'releases/%d' % release['id'])['assets']:
        if a['name'] == name and a['id'] != new_asset['id']:
            api(repo, 'releases/assets/%d' % a['id'], 'DELETE')
    api(repo, 'releases/assets/%d' % new_asset['id'], 'PATCH', {'name': name})


def releases(repo):
    out = gh('api', '--paginate', '--slurp', 'repos/%s/releases?per_page=100' % repo)
    pages = json.loads(out)
    return [r for page in pages for r in page]


def tag_commit(repo, tag):
    ref = api(repo, 'git/ref/tags/' + tag)
    obj = ref['object']
    while obj['type'] == 'tag':   # annotated tag
        obj = api(repo, 'git/tags/' + obj['sha'])['object']
    return obj['sha']


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def download_asset(repo, asset, dest):
    data = gh('api', '-H', 'Accept: application/octet-stream',
              'repos/%s/releases/assets/%d' % (repo, asset['id']), binary=True)
    with open(dest, 'wb') as f:
        f.write(data)
    if os.path.getsize(dest) != asset['size']:
        sys.exit('%s: downloaded %d bytes, GitHub says %d' % (asset['name'], os.path.getsize(dest), asset['size']))


def changelog_section(version):
    text = open(os.path.join(ROOT, 'CHANGELOG.md'), encoding='utf-8').read()
    m = re.search(r'^## %s(?=[ \t]|$)[^\n]*\n(.*?)(?=^## |\Z)' % re.escape(version), text, re.M | re.S)
    return m.group(1).strip() + '\n' if m else '(no section for %s in CHANGELOG.md)\n' % version


def version_key(name):
    """Natural order: v1.0.0-beta.10 after v1.0.0-beta.9."""
    return [p.zfill(10) if p.isdigit() else p for p in re.split(r'(\d+)', name)]


def write_index(stage, final):
    entries = []
    for name in sorted(os.listdir(stage)):
        d = os.path.join(stage, name)
        if os.path.isdir(d) and os.path.exists(os.path.join(d, 'entry.json')):
            entries.append(json.load(open(os.path.join(d, 'entry.json'), encoding='utf-8')))
    builds = os.path.join(stage, 'builds')
    if os.path.isdir(builds):
        for name in sorted(os.listdir(builds)):
            p = os.path.join(builds, name, 'entry.json')
            if os.path.exists(p):
                entries.append(json.load(open(p, encoding='utf-8')))
    entries.sort(key=lambda e: (e['kind'] != 'pre-release', version_key(e['tag'])))
    lines = ['# Pre-releases of VitaMoon %s' % final.lstrip('v'), '',
             'Kept here so that the Releases page lists only final versions. Each folder holds the',
             'VPK as it was published, its notes (`notes.md`) and, for those that had a GitHub release,',
             "the release's data as GitHub gave it (`release.json`: dates, author, download counts).",
             'The code of each one is still under its git tag.', '',
             '| Tag | Kind | Published | Commit | File | Size | SHA-256 | Downloads |',
             '|---|---|---|---|---|---|---|---|']
    for e in entries:
        for f in e['files']:
            lines.append('| `%s` | %s | %s | `%s` | `%s` | %d | `%s` | %s |' % (
                e['tag'], e['kind'], e['published'], e['commit'][:7], f['name'], f['size'], f['sha256'],
                f.get('downloads', '')))
    lines += ['', 'Archived on %s by `tools/dev/archive_betas.py`.' % datetime.date.today().isoformat(), '']
    with open(os.path.join(stage, 'README.md'), 'w', encoding='utf-8') as out:
        out.write('\n'.join(lines))
    return entries


def stage_release(repo, rel, folder, kind):
    os.makedirs(folder, exist_ok=True)
    files = []
    for asset in rel['assets']:
        dest = os.path.join(folder, asset['name'])
        download_asset(repo, asset, dest)
        files.append({'name': asset['name'], 'size': asset['size'], 'sha256': sha256(dest),
                      'downloads': asset['download_count']})
    with open(os.path.join(folder, 'notes.md'), 'w', encoding='utf-8') as f:
        f.write('# %s\n\n%s\n' % (rel['name'], rel['body'] or ''))
    with open(os.path.join(folder, 'release.json'), 'w', encoding='utf-8') as f:
        json.dump(rel, f, indent=2, ensure_ascii=False)
    entry = {'tag': rel['tag_name'], 'kind': kind, 'published': rel['published_at'],
             'commit': tag_commit(repo, rel['tag_name']), 'files': files}
    with open(os.path.join(folder, 'entry.json'), 'w', encoding='utf-8') as f:
        json.dump(entry, f, indent=2)
    return entry


def stage_parked(repo, asset, version, folder):
    os.makedirs(folder, exist_ok=True)
    dest = os.path.join(folder, asset['name'])
    download_asset(repo, asset, dest)
    tag = 'v' + version
    with open(os.path.join(folder, 'notes.md'), 'w', encoding='utf-8') as f:
        f.write('# VitaMoon %s\n\n%s' % (version, changelog_section(version)))
    entry = {'tag': tag, 'kind': 'pre-release', 'published': asset['created_at'],
             'commit': tag_commit(repo, tag),
             'files': [{'name': asset['name'], 'size': asset['size'], 'sha256': sha256(dest),
                        'downloads': asset['download_count']}]}
    with open(os.path.join(folder, 'entry.json'), 'w', encoding='utf-8') as f:
        json.dump(entry, f, indent=2)
    return entry


def build_zip(stage, path):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for base, dirs, names in os.walk(stage):
            dirs.sort(key=version_key)
            for n in sorted(names):
                full = os.path.join(base, n)
                z.write(full, os.path.relpath(full, stage))


def check_zip(path, entries, stage):
    """Every staged file is in the zip, byte for byte."""
    with zipfile.ZipFile(path) as z:
        if z.testzip() is not None:
            sys.exit('%s: damaged' % path)
        names = set(z.namelist())
        for e in entries:
            folder = e['tag'] if e['kind'] == 'pre-release' else 'builds/' + e['tag']
            for f in e['files']:
                member = folder + '/' + f['name']
                if member not in names:
                    sys.exit('%s: %s missing' % (path, member))
                if hashlib.sha256(z.read(member)).hexdigest() != f['sha256']:
                    sys.exit('%s: %s differs' % (path, member))
        for base, _, files in os.walk(stage):
            for n in files:
                member = os.path.relpath(os.path.join(base, n), stage).replace(os.sep, '/')
                if member not in names:
                    sys.exit('%s: %s missing' % (path, member))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo', default='LouisF37/VitaMoon')
    ap.add_argument('--final', required=True, help='tag of the final release, e.g. v1.0.0')
    ap.add_argument('--betas', action='append', default=[], help='tag prefix of pre-release releases')
    ap.add_argument('--from-dev-build', action='store_true', help='also the pre-release VPKs parked on build-main')
    ap.add_argument('--also', action='append', default=[], help='another release to keep whole, then delete')
    ap.add_argument('--apply', action='store_true', help='change GitHub (otherwise a dry run)')
    args = ap.parse_args()

    all_rel = releases(args.repo)
    by_tag = {r['tag_name']: r for r in all_rel}
    final = by_tag.get(args.final)
    if not final or final['prerelease'] or final['draft']:
        sys.exit('%s: no final release with this tag' % args.final)
    zip_name = 'vitamoon-%s-betas.zip' % args.final

    betas = sorted([r for r in all_rel if any(r['tag_name'].startswith(p) for p in args.betas)
                    and r['tag_name'] != args.final], key=lambda r: version_key(r['tag_name']))
    also = [by_tag[t] for t in args.also if t in by_tag]
    missing = [t for t in args.also if t not in by_tag]
    parked = []
    if args.from_dev_build and DEV_BUILD in by_tag:
        for a in by_tag[DEV_BUILD]['assets']:
            m = BETA_VPK.match(a['name'])
            if m:
                parked.append((a, m.group(1)))

    print('Final release: %s (%s)' % (args.final, final['name']))
    for r in betas:
        print('  pre-release  %-18s %d file(s), release deleted, tag kept' % (r['tag_name'], len(r['assets'])))
    for a, v in parked:
        print('  parked       %-18s %s, removed from %s' % ('v' + v, a['name'], DEV_BUILD))
    for r in also:
        print('  build        %-18s %d file(s), release and tag deleted' % (r['tag_name'], len(r['assets'])))
    for t in missing:
        print('  (no release %s)' % t)
    if not (betas or parked or also):
        print('Nothing to archive.')
        return

    work = tempfile.mkdtemp(prefix='betas-')
    stage = os.path.join(work, 'stage')
    os.makedirs(stage)
    existing = next((a for a in final['assets'] if a['name'] == zip_name), None)
    if existing:
        old = os.path.join(work, 'old.zip')
        download_asset(args.repo, existing, old)
        with zipfile.ZipFile(old) as z:
            z.extractall(stage)
        print('  (adding to the %s already on %s)' % (zip_name, args.final))

    for r in betas:
        stage_release(args.repo, r, os.path.join(stage, r['tag_name']), 'pre-release')
    for a, v in parked:
        stage_parked(args.repo, a, v, os.path.join(stage, 'v' + v))
    for r in also:
        stage_release(args.repo, r, os.path.join(stage, 'builds', r['tag_name']), 'build')
    entries = write_index(stage, args.final)
    zip_path = os.path.join(work, zip_name)
    build_zip(stage, zip_path)
    check_zip(zip_path, entries, stage)
    print('Zip: %s, %d bytes, %d entries' % (zip_name, os.path.getsize(zip_path), len(entries)))

    if not args.apply:
        print('Dry run: nothing changed on GitHub (zip left in %s).' % work)
        return

    # 1. Upload, then read back and check before deleting anything.
    uploaded = upload_asset(args.repo, final, zip_path)
    back = os.path.join(work, 'back.zip')
    download_asset(args.repo, uploaded, back)
    if sha256(back) != sha256(zip_path):
        sys.exit('the uploaded zip differs from the local one: nothing deleted (%s left on %s)'
                 % (uploaded['name'], args.final))
    check_zip(back, entries, stage)
    replace_asset(args.repo, final, uploaded, zip_name)
    print('Uploaded and checked.')

    # 2. A line in the final release's notes.
    body = final['body'] or ''
    if NOTE_MARKER not in body:
        tags = ', '.join('`%s`' % e['tag'] for e in entries if e['kind'] == 'pre-release')
        note = ('\n\n%s\n### Pre-releases\n\nThe pre-releases of this version (%s), with their VPK, notes and '
                'figures, are in `%s` below. Their code stays under their git tags.\n' % (NOTE_MARKER, tags, zip_name))
        api(args.repo, 'releases/%d' % final['id'], 'PATCH', {'body': body.rstrip() + note})

    # 3. Only now, remove what is archived.
    for r in betas:
        api(args.repo, 'releases/%d' % r['id'], 'DELETE')
        print('  deleted release %s (tag kept)' % r['tag_name'])
    for a, v in parked:
        api(args.repo, 'releases/assets/%d' % a['id'], 'DELETE')
        print('  removed %s from %s' % (a['name'], DEV_BUILD))
    for r in also:
        api(args.repo, 'releases/%d' % r['id'], 'DELETE')
        api(args.repo, 'git/refs/tags/' + r['tag_name'], 'DELETE')
        print('  deleted release and tag %s' % r['tag_name'])
    shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()

"""Checks tools/dev/archive_betas.py against an imitation of GitHub's API:
pre-release pages and parked VPKs end up in the zip with their notes and
checksums, a second run adds to the zip already there, a dry run changes
nothing, and nothing is deleted before the uploaded zip has been read back.

    python3 tools/dev/test_archive_betas.py
"""
import hashlib
import importlib.util
import io
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('archive_betas', os.path.join(HERE, 'archive_betas.py'))
ab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ab)

failures = 0


def check(cond, what):
    global failures
    print('  %s  %s' % ('ok  ' if cond else 'FAIL', what))
    if not cond:
        failures += 1


class FakeGitHub:
    """Releases, assets and tags in memory; records every change in order."""

    def __init__(self):
        self.next_id = 1
        self.releases = []
        self.blobs = {}
        self.tags = {}
        self.log = []
        self.corrupt_upload = False

    def _id(self):
        self.next_id += 1
        return self.next_id

    def add_release(self, tag, prerelease, files, body='notes of ' + 'x', commit=None):
        r = {'id': self._id(), 'tag_name': tag, 'name': 'VitaMoon ' + tag, 'prerelease': prerelease,
             'draft': False, 'body': body, 'published_at': '2026-11-16T10:00:00Z', 'assets': []}
        for name, data in files:
            self.add_asset(r, name, data)
        self.releases.append(r)
        self.tags[tag] = commit or hashlib.sha1(tag.encode()).hexdigest()
        return r

    def add_asset(self, r, name, data):
        a = {'id': self._id(), 'name': name, 'size': len(data), 'download_count': 3,
             'created_at': '2026-11-20T10:00:00Z'}
        self.blobs[a['id']] = data
        r['assets'].append(a)
        return a

    def release_by_tag(self, tag):
        return next((r for r in self.releases if r['tag_name'] == tag), None)

    def gh(self, *args, binary=False):
        args = list(args)
        if args[0] == 'api' and args[-1].startswith('https://uploads.github.com/'):
            rid = int(args[-1].split('/releases/')[1].split('/')[0])
            r = next(x for x in self.releases if x['id'] == rid)
            path = args[args.index('--input') + 1]
            data = open(path, 'rb').read()
            if self.corrupt_upload:
                data = data[:-1] + bytes([data[-1] ^ 0xFF])
            name = args[-1].split('name=')[1]
            assert all(a['name'] != name for a in r['assets']), 'name taken'
            a = self.add_asset(r, name, data)
            self.log.append(('upload', r['tag_name'], name))
            return json.dumps(a)
        assert args[0] == 'api', args
        if '--paginate' in args:
            return json.dumps([self.releases])
        if args[1] == '-H':
            asset_id = int(args[3].rsplit('/', 1)[1])
            return self.blobs[asset_id]
        method, path = args[2], args[3]
        fields = dict(f.split('=', 1) for f in args[5::2]) if len(args) > 4 else {}
        path = path.split('/', 3)[3]
        if path.startswith('git/ref/tags/'):
            return json.dumps({'object': {'type': 'commit', 'sha': self.tags[path[len('git/ref/tags/'):]]}})
        if path.startswith('git/refs/tags/') and method == 'DELETE':
            del self.tags[path[len('git/refs/tags/'):]]
            self.log.append(('delete-tag', path[len('git/refs/tags/'):]))
            return ''
        if path.startswith('releases/assets/') and method == 'PATCH':
            aid = int(path.rsplit('/', 1)[1])
            for r in self.releases:
                for a in r['assets']:
                    if a['id'] == aid:
                        a.update(fields)
            self.log.append(('rename-asset', aid, fields['name']))
            return '{}'
        if path.startswith('releases/assets/') and method == 'DELETE':
            aid = int(path.rsplit('/', 1)[1])
            name = next(a['name'] for r in self.releases for a in r['assets'] if a['id'] == aid)
            for r in self.releases:
                r['assets'] = [a for a in r['assets'] if a['id'] != aid]
            self.log.append(('delete-asset', name))
            return ''
        if path.startswith('releases/'):
            rid = int(path.split('/')[1])
            r = next(x for x in self.releases if x['id'] == rid)
            if method == 'DELETE':
                self.releases.remove(r)
                self.log.append(('delete-release', r['tag_name']))
                return ''
            if method == 'PATCH':
                r.update(fields)
                self.log.append(('edit', r['tag_name']))
            return json.dumps(r)
        raise AssertionError(path)


def run(fake, argv):
    ab.gh = fake.gh
    sys.argv = ['archive_betas.py', '--repo', 'o/r'] + argv
    try:
        ab.main()
        return None
    except SystemExit as e:
        return e.code


def zip_of(fake, final):
    r = fake.release_by_tag(final)
    a = next((a for a in r['assets'] if a['name'] == 'vitamoon-%s-betas.zip' % final), None)
    return zipfile.ZipFile(io.BytesIO(fake.blobs[a['id']])) if a else None


def main():
    print('[archive_betas]')
    fake = FakeGitHub()
    fake.add_release('v1.0.0', False, [('vitamoon-v1.0.0.vpk', b'final')], body='Final notes.')
    for n in (1, 2, 10):
        fake.add_release('v1.0.0-beta.%d' % n, True, [('vitamoon-v1.0.0-beta.%d.vpk' % n, b'beta%d' % n)],
                         body='Beta %d notes.' % n)
    fake.add_release('build-dev', True, [('moonlight-dev.vpk', b'dev')])
    fake.add_release('build-main', True, [('vitamoon-main.vpk', b'main')])

    before = json.dumps(fake.releases, sort_keys=True)
    run(fake, ['--final', 'v1.0.0', '--betas', 'v1.0.0-beta.', '--also', 'build-dev'])
    check(json.dumps(fake.releases, sort_keys=True) == before and not fake.log, 'a dry run changes nothing')

    fake.corrupt_upload = True
    code = run(fake, ['--final', 'v1.0.0', '--betas', 'v1.0.0-beta.', '--apply'])
    check(code is not None and not any(e[0].startswith('delete') or e[0] == 'rename-asset' for e in fake.log),
          'an upload that does not read back the same stops before any deletion')
    fake.corrupt_upload = False
    fake.log.clear()

    run(fake, ['--final', 'v1.0.0', '--betas', 'v1.0.0-beta.', '--also', 'build-dev', '--apply'])
    tags = [r['tag_name'] for r in fake.releases]
    check(tags == ['v1.0.0', 'build-main'], 'only the final release and the development build are left')
    check(all('v1.0.0-beta.%d' % n in fake.tags for n in (1, 2, 10)), 'the pre-releases keep their tags')
    check('build-dev' not in fake.tags, 'a removed rolling build loses its tag')
    check(fake.log[0] == ('delete-asset', 'vitamoon-v1.0.0-betas.zip.new'),
          'the upload left by the stopped run is cleared first')
    upload = fake.log.index(('upload', 'v1.0.0', 'vitamoon-v1.0.0-betas.zip.new'))
    renamed = next(i for i, e in enumerate(fake.log) if e[0] == 'rename-asset')
    check(all(not e[0].startswith('delete') for e in fake.log[1:upload]) and
          all(e[0] != 'delete-release' and e[0] != 'delete-tag' for e in fake.log[:renamed]),
          'nothing is deleted before the zip is uploaded, read back and named')
    check([a['name'] for a in fake.release_by_tag('v1.0.0')['assets']] ==
          ['vitamoon-v1.0.0.vpk', 'vitamoon-v1.0.0-betas.zip'], 'one zip, under its final name')
    z = zip_of(fake, 'v1.0.0')
    names = z.namelist()
    check(all('v1.0.0-beta.%d/vitamoon-v1.0.0-beta.%d.vpk' % (n, n) in names for n in (1, 2, 10)),
          'every pre-release VPK is in the zip')
    check(z.read('v1.0.0-beta.2/vitamoon-v1.0.0-beta.2.vpk') == b'beta2', 'byte for byte')
    check('Beta 10 notes.' in z.read('v1.0.0-beta.10/notes.md').decode(), 'with its notes')
    check(json.loads(z.read('v1.0.0-beta.1/release.json'))['tag_name'] == 'v1.0.0-beta.1', 'and its release data')
    check('builds/build-dev/moonlight-dev.vpk' in names, 'the rolling build is kept under builds/')
    index = z.read('README.md').decode()
    check(index.index('beta.2`') < index.index('beta.10`'), 'the index lists beta.10 after beta.2')
    check(hashlib.sha256(b'beta1').hexdigest() in index, 'with each file\'s SHA-256')
    final = fake.release_by_tag('v1.0.0')
    check(final['body'].startswith('Final notes.') and 'vitamoon-v1.0.0-betas.zip' in final['body'],
          'the final release\'s notes point to the zip')

    # Later: CI parks the VPK of a pre-release version on the development build.
    fake.log.clear()
    dev = fake.release_by_tag('build-main')
    fake.tags['v1.0.0-rc.1'] = 'c' * 40
    fake.add_asset(dev, 'vitamoon-v1.0.0-rc.1.vpk', b'rc1')
    run(fake, ['--final', 'v1.0.0', '--from-dev-build', '--apply'])
    z = zip_of(fake, 'v1.0.0')
    up = fake.log.index(('upload', 'v1.0.0', 'vitamoon-v1.0.0-betas.zip.new'))
    old_zip = fake.log.index(('delete-asset', 'vitamoon-v1.0.0-betas.zip'))
    check(up < old_zip < next(i for i, e in enumerate(fake.log) if e[0] == 'rename-asset'),
          'the older zip is deleted only once the new one has been read back')
    check('v1.0.0-rc.1/vitamoon-v1.0.0-rc.1.vpk' in z.namelist(), 'a parked VPK joins the zip')
    check('v1.0.0-beta.10/vitamoon-v1.0.0-beta.10.vpk' in z.namelist(), 'what the zip had is kept')
    check([a['name'] for a in fake.release_by_tag('build-main')['assets']] == ['vitamoon-main.vpk'],
          'and leaves the development build with its own VPK only')
    check(fake.release_by_tag('v1.0.0')['body'].count(ab.NOTE_MARKER) == 1, 'the note is written once')

    fake.log.clear()
    run(fake, ['--final', 'v1.0.0', '--from-dev-build', '--apply'])
    check(not fake.log, 'nothing parked: nothing changes')

    print('[archive_betas] %s' % ('all passed' if not failures else '%d FAILED' % failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())

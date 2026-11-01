#!/usr/bin/env python3
"""A release on this PC, to try the game list's updates without GitHub (frontend/updater.cpp).

    python3 tools/updater/fake_release.py FILE [FILE ...] [--port 8765] [--bad-hash] [--no-sums]

Serves, on 127.0.0.1, what GitHub's list of releases would say about these files (the PC
downloads, by their release names: pokemon-ds-wide-pc-windows-x86_64.zip and so on), the files
and their SHA256SUMS.txt. Start the program with FULLDEX_UPDATE_URL set to the address it prints;
the program then takes its updates from here, and only from here.

--bad-hash: the sums do not match (the program must refuse the download and change nothing).
--no-sums:  no SHA256SUMS.txt in the release (the program must only say there is a new version).
The files are said to be uploaded a day from now, so they count as newer than the program.
"""
import argparse
import datetime
import hashlib
import http.server
import json
import os
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+')
    ap.add_argument('--port', type=int, default=8765)
    ap.add_argument('--bad-hash', action='store_true')
    ap.add_argument('--no-sums', action='store_true')
    args = ap.parse_args()

    base = 'http://127.0.0.1:%d' % args.port
    files = {}
    for path in args.files:
        name = os.path.basename(path)
        if not name.startswith('pokemon-ds-wide-'):
            sys.exit('%s: not a release file (pokemon-ds-wide-...)' % path)
        files[name] = os.path.abspath(path)
    when = (datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=1)).strftime('%Y-%m-%dT%H:%M:%SZ')
    sums = ''
    for name, path in sorted(files.items()):
        with open(path, 'rb') as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        sums += '%s  %s\n' % ('0' * 64 if args.bad_hash else digest, name)
    assets = [{'name': n, 'size': os.path.getsize(p), 'updated_at': when,
               'browser_download_url': '%s/download/%s' % (base, n)} for n, p in sorted(files.items())]
    if not args.no_sums:
        assets.append({'name': 'SHA256SUMS.txt', 'size': len(sums), 'updated_at': when,
                       'browser_download_url': base + '/download/SHA256SUMS.txt'})
    releases = json.dumps([{'tag_name': 'pc-test', 'html_url': base + '/', 'draft': False,
                            'prerelease': False, 'assets': assets}], indent=1).encode()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == '/releases.json':
                body, kind = releases, 'application/json'
            elif self.path == '/download/SHA256SUMS.txt' and not args.no_sums:
                body, kind = sums.encode(), 'text/plain'
            elif self.path.startswith('/download/') and self.path[10:] in files:
                with open(files[self.path[10:]], 'rb') as f:
                    body, kind = f.read(), 'application/octet-stream'
            elif self.path == '/':
                body, kind = b'<h1>Pokemon Fulldex: a test release on this PC</h1>', 'text/html'
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', kind)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    print('Release with: %s' % ', '.join(sorted(files)))
    print(sums, end='')
    print('Start the program with:  FULLDEX_UPDATE_URL=%s/releases.json' % base)
    print('(Ctrl+C stops this.)')
    sys.stdout.flush()
    http.server.ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()


if __name__ == '__main__':
    main()

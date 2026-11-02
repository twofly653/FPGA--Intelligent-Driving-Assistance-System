"""Fetch pinned official CUDA wheels using validated/resumable HTTP ranges."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import html
import json
from pathlib import Path
import re
import threading
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parent
DEST = ROOT / '.cache/wheels'
DEST.mkdir(parents=True, exist_ok=True)
CHUNK = 8 * 1024 * 1024

def get(req, timeout=35):
    return urllib.request.urlopen(req, timeout=timeout)

def checksum(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(CHUNK), b''):
            h.update(block)
    return h.hexdigest()

def fetch(package, filename):
    index = f'https://download.pytorch.org/whl/cu128/{package}/'
    with get(index) as response:
        page = response.read().decode()
    links = re.findall(r'href="([^"]+)"', page)
    link = next(html.unescape(x) for x in links if filename in urllib.parse.unquote(x))
    raw_url, fragment = urllib.parse.urldefrag(urllib.parse.urljoin(index, link))
    expected_sha = urllib.parse.parse_qs(fragment)['sha256'][0]
    # The official mirror supports bounded responses; validate every range.
    url = raw_url.replace('download-r2.pytorch.org', 'download.pytorch.org')
    path = DEST / filename
    if path.exists() and checksum(path) == expected_sha:
        print('Already verified ' + filename, flush=True)
        return
    request = urllib.request.Request(url, headers={'Range': 'bytes=0-0'})
    with get(request) as response:
        assert response.status == 206
        total = int(response.headers['Content-Range'].split('/')[-1])
    part = path.with_suffix('.whl.part')
    resume = path.with_suffix('.progress.json')
    done = set()
    if part.exists() and part.stat().st_size == total and resume.exists():
        record = json.loads(resume.read_text())
        if record.get('sha256') == expected_sha:
            done = set(record['done'])
    else:
        with part.open('wb') as f:
            f.truncate(total)
    lock = threading.Lock()
    count = (total + CHUNK - 1) // CHUNK
    print(json.dumps({'file': filename, 'bytes': total, 'chunks': count, 'resumed': len(done)}), flush=True)

    def segment(i):
        start, end = i * CHUNK, min(total, (i + 1) * CHUNK) - 1
        last_error = None
        for attempt in range(4):
            try:
                # Distinct URLs prevent intermediary caches ignoring Range.
                req = urllib.request.Request(url + f'?segment={i}',
                                             headers={'Range': f'bytes={start}-{end}'})
                with get(req) as response:
                    assert response.status == 206
                    assert response.headers['Content-Range'] == f'bytes {start}-{end}/{total}'
                    block = response.read(end - start + 2)
                    assert len(block) == end - start + 1
                with part.open('r+b') as f:
                    f.seek(start)
                    f.write(block)
                with lock:
                    done.add(i)
                    resume.write_text(json.dumps({'sha256': expected_sha, 'done': sorted(done)}))
                    if len(done) % 8 == 0 or len(done) == count:
                        print(f'{filename}: {len(done)}/{count} chunks ({len(done)*100/count:.1f}%)', flush=True)
                return
            except Exception as exc:
                last_error = exc
        raise RuntimeError(f'Chunk {i} failed: {last_error}')

    with ThreadPoolExecutor(max_workers=12) as pool:
        futures = [pool.submit(segment, i) for i in range(count) if i not in done]
        for future in as_completed(futures):
            future.result()
    sha = hashlib.sha256()
    with part.open('rb') as f:
        for block in iter(lambda: f.read(CHUNK), b''):
            sha.update(block)
    assert sha.hexdigest() == expected_sha, 'Official wheel hash mismatch'
    part.replace(path)
    path.with_suffix('.source.json').write_text(json.dumps({'url': raw_url, 'index': index,
                                                          'sha256': expected_sha, 'bytes': total}, indent=2))
    print('SHA256 verified: ' + filename, flush=True)

fetch('torch', 'torch-2.9.1+cu128-cp311-cp311-win_amd64.whl')
fetch('torchvision', 'torchvision-0.24.1+cu128-cp311-cp311-win_amd64.whl')

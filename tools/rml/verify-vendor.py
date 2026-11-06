"""Verify pinned dependency bytes without modifying or fetching dependencies."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
for package in ('rmlui', 'freetype'):
    directory = root / 'external' / package
    manifest = json.loads((directory / 'provenance.json').read_text(encoding='utf-8'))
    expected = manifest['files']
    actual = {str(p.relative_to(directory)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in (directory / 'vendor').rglob('*') if p.is_file()}
    if actual != expected:
        changed = sorted(key for key in expected.keys() | actual.keys() if expected.get(key) != actual.get(key))
        raise SystemExit(f'{package}: vendor manifest mismatch: {changed}')
    print(f'{package} {manifest["version"]}: {len(actual)} pinned files verified')

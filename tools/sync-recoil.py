"""Synchronize recoil core, validation fixtures and tooling between selected source repos."""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', type=Path)
    parser.add_argument('--mod', type=Path)
    parser.add_argument('--release', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    client = Path(__file__).resolve().parents[1]
    explicit = any((args.server, args.mod, args.release))
    server = args.server or (client.parent / 'CoD4x_Server' if not explicit else None)
    mod = args.mod or (client.parent / 'promod-econ-na' if not explicit else None)
    release = args.release or (client.parent / 'cod4-promod' if not explicit else None)
    corpus = client / 'tests/fixtures/recoil-contracts.tsv'
    pairs = []
    if server and server.is_dir():
        pairs += [(client / 'src/recoil/model.h', server / 'src/recoil/model.h'),
                  (corpus, server / 'tools/tests/recoil-contracts.tsv')]
    if mod and mod.is_dir():
        pairs += [(corpus, mod / 'tools/tests/recoil-contracts.tsv')]
    if release and release.is_dir():
        module = (mod or client.parent / 'promod-econ-na') / 'tools/recoil_contract.py'
        pairs += [(module, release / 'scripts/recoil_contract.py'),
                  (corpus, release / 'scripts/fixtures/recoil-contracts.tsv')]
    if not pairs:
        raise SystemExit('Pass a source repository path with --server, --mod or --release.')
    for source, target in pairs:
        if args.check:
            if not target.is_file() or target.read_bytes() != source.read_bytes():
                raise SystemExit(f'Recoil mirror differs: {target}; run tools/sync-recoil.py.')
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.read_bytes())
    print(f'Recoil mirrors {"verified" if args.check else "updated"}: {len(pairs)} files.')


if __name__ == '__main__':
    main()

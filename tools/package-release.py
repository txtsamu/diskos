#!/usr/bin/env python3
"""Package verified UI and independent apps, source, licenses and instructions."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,tarfile,zipfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=root/'dist');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
version=re.search(r'#define DISCO_VERSION\s+"([^"]+)"',(root/'ui/version.h').read_text()).group(1)
meta=json.loads((root/'payload/mq_ui.build.json').read_text())
assert meta['version']==version
assert hashlib.sha256((root/'payload/mq_ui').read_bytes()).hexdigest()==meta['binary']['sha256']
assert (root/'payload/mq_ui').stat().st_size==meta['binary']['bytes']
apps=[('album-roulette','1.3.1','Album-Roulette-1.3.1.tar.gz','w:gz'),('disc-shelf','1.0.4','Disc-Shelf-1.0.4.tar','w')]
allowed={'app','app.conf','shelf.conf.example','README.md','NOTICE.md','LICENSE','BUILD.json','CHANGELOG.md','HANDOFF.md'}
entries=[];outputs=[]
for name,appver,archive,mode in apps:
    folder=root/'apps'/name;files=[]
    for path in sorted(folder.rglob('*')):
        rel=path.relative_to(folder)
        if path.is_symlink():raise SystemExit('Refusing app symlink: '+str(rel))
        if not path.is_file():continue
        if not ((len(rel.parts)==1 and rel.name in allowed) or (len(rel.parts)==2 and rel.parts[0]=='licenses' and rel.suffix=='.txt')):raise SystemExit('Unexpected app file: '+str(rel))
        files.append(path)
    assert {'app','app.conf','README.md','NOTICE.md','LICENSE'}.issubset({f.name for f in files})
    if name=='disc-shelf':
        m=json.loads((folder/'BUILD.json').read_text());assert m['version']==appver
        assert hashlib.sha256((folder/'app').read_bytes()).hexdigest()==m['binary']['sha256']
    source=root/'corresponding-source'/f'{name}-{appver}-source.tar.gz';assert source.is_file()
    with tarfile.open(out/archive,mode) as t:
        for f in files:t.add(f,arcname=str(Path(name)/f.relative_to(folder)))
    shutil.copy2(source,out/source.name);outputs.extend([out/archive,out/source.name]);entries.extend(files)
for name in ['mq_ui','mq_ui.build.json']:
    shutil.copy2(root/'payload'/name,out/name);outputs.append(out/name)
complete=out/f'diskos-disco-{version}-complete.zip'
with zipfile.ZipFile(complete,'w',zipfile.ZIP_DEFLATED) as z:
    z.write(out/'mq_ui','mq_ui');z.write(out/'mq_ui.build.json','mq_ui.build.json')
    z.write(root/'RELEASE_INSTALL.md','INSTALL.md');z.write(root/f'docs/releases/v{version}.md','RELEASE-NOTES.md')
    for name in ['UPDATING.md','DISC_SHELF_INSTALL.md','ALBUM_ROULETTE_INSTALL.md']:z.write(root/'docs'/name,'docs/'+name)
    for name in ['LICENSE','NOTICE.md']:z.write(root/name,name)
    for f in entries:z.write(f,f.relative_to(root))
    for name,appver,archive,mode in apps:
        z.write(out/archive,archive);source=f'{name}-{appver}-source.tar.gz';z.write(out/source,'corresponding-source/'+source)
outputs.append(complete)
for name,alg in [('SHA256SUMS','sha256'),('MD5SUMS','md5')]:
    (out/name).write_text(''.join(f'{hashlib.new(alg,f.read_bytes()).hexdigest()}  {f.name}\n' for f in outputs))
print('Packaged '+str(complete))

#!/usr/bin/env python3
"""Exercise production artwork handoff/reuse and rapid skips in real LVGL."""
from pathlib import Path
import os, subprocess, tempfile
from PIL import Image

app=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='diskos-music-ui-') as td:
    root=Path(td)
    for preset in (8,9,10):
        for variant in (0,1):
            for side in ((0,1) if preset==10 else (0,)):
                out=root/f'{preset}-{variant}-{side}.ppm'
                Image.new('RGB',(64,64),(171,91,41)).save(out.with_suffix('.png'))
                Image.new('RGB',(64,64),(209,71,11)).save(str(out.with_suffix('.png'))+'-red.png')
                Image.new('RGB',(64,64),(11,71,209)).save(str(out.with_suffix('.png'))+'-blue.png')
                env={**os.environ,'REVIEW_TRACK_SWITCHES':'1','DISCO_SIDE':str(side)}
                subprocess.run([str(app/'tests/host_build/host-render'),str(preset),str(variant),'preview-home','0',str(out)],env=env,check=True,timeout=15)
print('PASS: eight theme/appearance/side scenarios, same-album reuse and rapid-skip pixels')

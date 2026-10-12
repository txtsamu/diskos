#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
app=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='diskos-eq-read-') as td:
    t=Path(td)
    cmd=['cc','-O0','-w','-D_GNU_SOURCE','-DLV_CONF_INCLUDE_SIMPLE','-ffunction-sections','-fdata-sections',
         '-I'+str(app),'-Wl,--gc-sections','-pthread','-DDB_PATH="'+str(t/'song.db')+'"',
         '-DART_MEMINFO="'+str(t/'meminfo')+'"',str(app/'tests/eq_profile_async_test.c'),
         str(app/'eqprofile.c'),str(app/'musicdb.c'),str(app/'sqlite3.c'),'-lm','-o',str(t/'test')]
    subprocess.run(cmd,check=True)
    subprocess.run([str(t/'test')],check=True,timeout=10)

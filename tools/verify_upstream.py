#!/usr/bin/env python3
"""Ensure disabled Tall still matches the pinned unmodified upstream core."""
import argparse,subprocess,os,json
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--pristine',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();root=Path(__file__).resolve().parent.parent;out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);records=[]
cases=[('mode'+str(m),['--mode',str(m)],{})for m in range(8)]+[('mode7-affine',['--mode','7','--affine'],{}),('mode7-mosaic',['--mode','7','--mosaic'],{}),('mode7-interlace',['--mode','7','--interlace'],{}),('mode1-interlace',['--interlace'],{})]+[('mode7-hires-'+v,['--mode','7'],{'TALL_HIRES':v})for v in['2x','4x','2x_hv','4x_hv']]
for name,opts,extra in cases:
 rom=out/(name+'.sfc');subprocess.run(['python3',str(root/'tools/make_diagnostic.py'),'--out',str(rom),*opts],check=True,stdout=subprocess.DEVNULL)
 for label,lib in [('tall',root/'libretro'),('upstream',a.pristine)]:
  env=os.environ.copy();env.update(extra);env['DYLD_LIBRARY_PATH']=str(lib.resolve());prefix=out/(name+'-'+label);r=subprocess.run([str(a.binary.resolve()),str(rom),'disabled',str(prefix),'180','down'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);assert r.returncode==0;(out/(name+'-'+label+'.log')).write_bytes(r.stdout)
 for suffix in ['-180.ppm','.state']:
  aa=(out/(name+'-tall'+suffix)).read_bytes();bb=(out/(name+'-upstream'+suffix)).read_bytes();assert aa==bb,(name,suffix,'upstream mismatch')
 records.append({'name':name,'native_pixels_identical_to_upstream':True,'full_state_identical_to_upstream':True});print(name,'upstream PASS',flush=True)
(out/'results.json').write_text(json.dumps({'passed':len(records),'cases':records},indent=2))

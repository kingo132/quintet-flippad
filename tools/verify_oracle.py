#!/usr/bin/env python3
"""Compare extra bands with independently camera-shifted native rendering."""
import argparse,os,subprocess,json
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();root=Path(__file__).resolve().parent.parent;out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);env=os.environ.copy();env['DYLD_LIBRARY_PATH']=str(root/'libretro');rows=[]
def render(mode,camera,height,label):
 rom=out/(label+'.sfc');subprocess.run(['python3',str(root/'tools/make_diagnostic.py'),'--mode',str(mode),'--camera',str(camera),'--no-objects',*(['--affine']if 'affine'in label else[]),'--out',str(rom)],check=True,stdout=subprocess.DEVNULL)
 p=out/label;r=subprocess.run([str(a.binary.resolve()),str(rom),str(height),str(p),'120','none'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);assert r.returncode==0;(out/(label+'.log')).write_bytes(r.stdout)
 hdr,b=Path(str(p)+'-120.ppm').read_bytes().split(b'\n255\n',1);w,h=map(int,hdr.splitlines()[1].split());assert w==256;return h,b
for mode in(0,1,2,3,4,7,17):
 affine=mode==17;mode=7 if affine else mode;tag=f'mode{mode}'+('-affine'if affine else'')
 h,extra=render(mode,0,'auto',tag+'-expanded');assert h==416
 period=1024 if mode==7 else 512
 _,top=render(mode,-96,'disabled',tag+'-top-reference')
 _,bottom=render(mode,224,'disabled',tag+'-bottom-reference')
 assert extra[:96*256*3]==top[:96*256*3],(mode,'top background differs from independently shifted native camera')
 assert extra[320*256*3:]==bottom[:96*256*3],(mode,'bottom background differs from independently shifted native camera')
 rows.append(dict(mode=mode,affine=affine,top_native_camera_match=True,bottom_native_camera_match=True));print(mode,'independent native camera oracle PASS',flush=True)
(out/'results.json').write_text(json.dumps({'passed':len(rows),'cases':rows},indent=2))

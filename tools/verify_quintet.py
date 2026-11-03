#!/usr/bin/env python3
"""Private-ROM integration check: native pixels/state and WRAM/VRAM map audit.
Requires a verify_pair host built against this core. No game data is bundled.
"""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--binary',type=Path,required=True);p.add_argument('--rom',type=Path,required=True)
p.add_argument('--profile',type=int,choices=[1,2],required=True);p.add_argument('--out',type=Path,required=True)
p.add_argument('--frames',type=int,default=600);p.add_argument('--state',type=Path);p.add_argument('--script',type=Path)
p.add_argument('--present-every',type=int,default=1)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);out=a.out.resolve();env=os.environ.copy()
env.update(QUINTET_PROFILE=str(a.profile),QUINTET_AUDIT='1',QUINTET_PRESENT_EVERY=str(a.present_every))
if a.state:env['TALL_STATE_IN']=str(a.state.resolve())
if a.script:env['QUINTET_SCRIPT']=str(a.script.resolve())
run=subprocess.run([str(a.binary.resolve()),str(a.rom.resolve()),'416',str(out/'expanded'),str(a.frames),'script'],env=env,capture_output=True)
(out/'run.log').write_bytes(run.stdout+run.stderr);assert run.returncode==0,run.stderr.decode(errors='replace')[-1000:]
state=(out/'expanded.state').read_bytes();assert state==(out/'expanded-native.state').read_bytes(),'game state changed by presentation'
def ppm(path):
 header,data=path.read_bytes().split(b'\n255\n',1);w,h=map(int,header.splitlines()[1].split());assert len(data)==w*h*3;return w,h,data
count=0
for path in out.glob('expanded-[0-9]*.ppm'):
 n=path.stem.split('-')[-1];w,h,extra=ppm(path);nw,nh,native=ppm(out/('expanded-native-'+n+'.ppm'));assert(nw,nh)==(256,224)
 # HUD/frame/one reserved scanline are deliberately relocated/rebuilt.
 x0,x1,y0,y1=(0,256,32,223)if a.profile==1 else(8,248,32,216)
 left=(w-256)//2;top=(h-224)//2
 for y in range(y0,y1):
  assert extra[((top+y)*w+left+x0)*3:((top+y)*w+left+x1)*3]==native[(y*256+x0)*3:(y*256+x1)*3],('native interior differs',n,y)
 count+=1
matches=re.search(rb'map_words_matching=(\d+) map_words_compared=(\d+)',run.stdout)
assert matches and matches[1]==matches[2], 'full-map path differs from native VRAM'
result=dict(profile=a.profile,frames=a.frames,native_snapshots=count,complete_state_identical=True,native_interior_identical=True,map_words_matching=int(matches[1]),map_words_compared=int(matches[2]),present_every=a.present_every)
(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

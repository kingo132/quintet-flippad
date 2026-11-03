#!/usr/bin/env python3
"""Compare identical core/input runs with and without presentation extension."""
import argparse,os,subprocess,json,hashlib
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();root=Path(__file__).resolve().parent.parent;out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);env=os.environ.copy();env['DYLD_LIBRARY_PATH']=str(root/'libretro');records=[]
def make(name,*args):
 p=out/(name+'.sfc');subprocess.run(['python3',str(root/'tools/make_diagnostic.py'),'--out',str(p),*args],check=True,stdout=subprocess.DEVNULL);return p
def image(p):
 h,b=p.read_bytes().split(b'\n255\n',1);w,y=map(int,h.splitlines()[1].split());assert len(b)==w*y*3;return w,y,b

def case(name,rom,height='auto',expected=None,opts=None,sprites=False):
 p=out/name;e=env.copy();e.update(opts or {});r=subprocess.run([str(a.binary.resolve()),str(rom),str(height),str(p),'180','down'],env=e,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/(name+'.log')).write_bytes(r.stdout);assert r.returncode==0,(name,r.stdout[-1000:]);w,h,b=image(Path(str(p)+'-180.ppm'));nw,nh,n=image(Path(str(p)+'-native-180.ppm'))
 if expected:assert(w,h)==expected,(name,(w,h),expected)
 top=(h-nh)//2;assert w==nw and top>=0;assert b[top*w*3:(top+nh)*w*3]==n,(name,'native pixels differ')
 s=Path(str(p)+'.state').read_bytes();base=Path(str(p)+'-native.state').read_bytes();assert s==base,(name,'serialized emulation state differs')
 if sprites:
  # Distinct solid OAM pixels wholly beyond the original screen, not
  # translated copies of anything in the native image.
  pixels=lambda raw:set(zip(raw[::3],raw[1::3],raw[2::3]))
  assert (255,0,0)in pixels(b[:top*w*3]),(name,'top OAM marker absent')
  assert (0,255,0)in pixels(b[(top+nh)*w*3:]),(name,'bottom OAM marker absent')
  assert (255,0,0)not in pixels(n) and (0,255,0)not in pixels(n)
 if opts and opts.get('TALL_SPRITES')=='disabled':
  colors=set(zip(b[::3],b[1::3],b[2::3]));assert(255,0,0)not in colors and(0,255,0)not in colors
 records.append(dict(name=name,width=w,height=h,native_height=nh,native_pixels_identical=True,full_state_identical=True,state_sha256=hashlib.sha256(s).hexdigest()))
 print(name,w,h,'PASS',flush=True)
for mode in (0,1,2,3,4,7):
 rom=make('mode'+str(mode),'--mode',str(mode))
 for h in ('auto',256,320,384,416,440,480,576,1024,1648,2048):case('mode%d-%s'%(mode,h),rom,h,(256,416 if h=='auto'else h),sprites=h!='auto'and h>=256 or h=='auto')
case('sprites-off',make('sprites-off'),opts={'TALL_SPRITES':'disabled'},expected=(256,416))
case('no-bg1',make('no-bg1'),opts={'TALL_LAYERS':'no_bg1'},expected=(256,416),sprites=True)
case('only-bg1',make('only-bg1'),opts={'TALL_LAYERS':'bg1'},expected=(256,416),sprites=True)
case('pal',make('pal','--pal'),expected=(256,416),sprites=True)
for mode in (5,6):case('fallback-mode'+str(mode),make('fallback-mode'+str(mode),'--mode',str(mode)),expected=(256,224))
case('fallback-mosaic',make('fallback-mosaic','--mosaic'),expected=(256,224))
case('fallback-interlace',make('fallback-interlace','--interlace'))
case('overscan239',make('overscan239','--overscan'),expected=(256,416),sprites=True)
case('overscan239-small',make('overscan239-small','--overscan'),256,expected=(256,256),sprites=True)
case('overscan239-mode7',make('overscan239-mode7','--mode','7','--overscan'),expected=(256,416),sprites=True)
case('overscan239-interlace',make('overscan239-interlace','--overscan','--interlace'),expected=(256,478))
case('fallback-ntsc',make('fallback-ntsc'),opts={'TALL_NTSC':'1'})
(out/'results.json').write_text(json.dumps({'passed':len(records),'cases':records},indent=2));print('PASS',len(records))

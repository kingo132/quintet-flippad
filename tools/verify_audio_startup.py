#!/usr/bin/env python3
"""Private-ROM regression: a cold SPC upload must survive accelerated startup.
Unlike loading an already-playing fixture, this catches the initialization race.
Outputs contain private PCM/state data and must never be committed.
"""
import argparse,os,subprocess,array,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);p.add_argument('--soul-rom',type=Path,required=True);p.add_argument('--terra-rom',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--frames',type=int,default=6000);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);results=[]
for game,rom in [('soul',a.soul_rom),('terra',a.terra_rom)]:
 for speed in [1,2,4]:
  prefix=(a.out/(game+'-'+str(speed))).resolve();pcm=prefix.with_suffix('.pcm');env=os.environ.copy();env.update(QUINTET_SPEED=str(speed),QUINTET_PCM=str(pcm));env.pop('TALL_STATE_IN',None)
  run=subprocess.run([str(a.binary.resolve()),str(rom.resolve()),'disabled',str(prefix),str(a.frames),'name-b'],env=env,capture_output=True);prefix.with_suffix('.log').write_bytes(run.stdout+run.stderr);assert run.returncode==0,game
  samples=array.array('h');samples.frombytes(pcm.read_bytes()[-32040*2*2:]);rms=(sum(x*x for x in samples)/len(samples))**.5
  assert rms>10,(game,speed,'cold-start audio remains silent',rms)
  results.append(dict(game=game,speed=speed,frames=a.frames,tail_rms=rms))
(a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results))

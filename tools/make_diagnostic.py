#!/usr/bin/env python3
"""Original LoROM: 512-row resident map, off-native OAM markers, joypad scroll.
No commercial assets. Produces normal and fallback-mode fixtures.
"""
import argparse,struct
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--mode',type=int,default=1);ap.add_argument('--out',type=Path);ap.add_argument('--affine',action='store_true');ap.add_argument('--camera',type=int,default=0);ap.add_argument('--no-objects',action='store_true');ap.add_argument('--pal',action='store_true');ap.add_argument('--overscan',action='store_true');ap.add_argument('--interlace',action='store_true');ap.add_argument('--mosaic',action='store_true');a=ap.parse_args()
root=Path(__file__).resolve().parent.parent;rom=bytearray([0]*131072);code=bytearray();labels={};fix=[]
def emit(*b):code.extend(b)
def imm(v):emit(0xa9,v&255)
def sta(addr):emit(0x8d,addr&255,addr>>8)
def stz(addr):emit(0x9c,addr&255,addr>>8)
def put(addr,v):imm(v);sta(addr)
def label(n):labels[n]=len(code)
def branch(op,n):emit(op,0);fix.append((len(code)-1,n))
def dma(src,length,dest,mode=0,bank=0):
 for addr,val in [(0x4300,mode),(0x4301,dest),(0x4302,src&255),(0x4303,src>>8),(0x4304,bank),(0x4305,length&255),(0x4306,length>>8)]:put(addr,val)
 put(0x420b,1)
emit(0x78,0xd8,0x18,0xfb,0xc2,0x30,0xa2,0xff,0x1f,0x9a,0xe2,0x20) # native, A8, XY16, stack
put(0x2100,0x80);stz(0x4200);stz(0x420c);stz(0x212c);stz(0x212d);stz(0x2130);stz(0x2131);stz(0x2132)
put(0x2105,a.mode);put(0x2107,2);put(0x210b,1);put(0x2115,0x80);stz(0x2116);stz(0x2117)
dma(0x9000,4096,0x18,1)
put(0x2116,0);put(0x2117,0x10)
depth=2 if a.mode==0 else 8 if a.mode==3 else 4
chrdata=bytearray()
for tile in range(16):
 for plane in range(0,depth,2):
  for y in range(8):
   # Inset rows and columns expose both tile boundaries and pixel edges.
   c=tile if y not in (0,7)else 0
   chrdata.extend([0x7e if c&(1<<plane)else 0,0x7e if c&(1<<(plane+1))else 0])
rom[0x2000:0x2000+len(chrdata)]=chrdata
dma(0xa000,len(chrdata),0x18,1)
if a.mode in (2,4):
 offsets=bytearray(2048)
 for y in range(32):
  for x in range(32):
   val=(0xa000|((60+a.camera)&1023))if a.mode==4 and x%2 else(0x2000|(((60+a.camera)&1023) if a.mode==2 and y%2 else 40))
   struct.pack_into('<H',offsets,(y*32+x)*2,val)
 rom[0x3800:0x4000]=offsets;put(0x2109,0x18);put(0x2116,0);put(0x2117,0x18);dma(0xb800,2048,0x18,1)
if a.mode==7:
 m7=bytearray(65536)
 for y in range(128):
  for x in range(128):m7[(y*128+x)*2]=1+(y%63)
 for tile in range(1,65):
  for y in range(8):
   for x in range(8):m7[(tile*64+y*8+x)*2+1]=tile if 0<x<7 and 0<y<7 else 0
 rom[0x8000:0x18000]=m7;put(0x2116,0);put(0x2117,0);dma(0x8000,32768,0x18,1,1)
 put(0x2116,0);put(0x2117,0x40);dma(0x8000,32768,0x18,1,2)
 for addr,val in [(0x211b,0xc0 if a.affine else 0x100),(0x211c,0x80 if a.affine else 0),(0x211d,-0x80 if a.affine else 0),(0x211e,0xc0 if a.affine else 0x100),(0x211f,0),(0x2120,0)]:put(addr,val&255);put(addr,val>>8)

# Separate OBJ tile bank ($8000 bytes) with three original solid markers.
obj=bytearray(32*4)
for tile,color in [(1,1),(2,2),(3,3)]:
 for y in range(8):
  obj[tile*32+y*2]=255 if color&1 else 0;obj[tile*32+y*2+1]=255 if color&2 else 0
rom[0x2800:0x2800+len(obj)]=obj
put(0x2116,0);put(0x2117,0x40);dma(0xa800,len(obj),0x18,1);put(0x2101,2)
palette=bytearray(512)
for i in range(1,128):
 v=((i*7)%31+1)|(((i*11)%31+1)<<5)|(((i*17)%31+1)<<10);struct.pack_into('<H',palette,i*2,v)
for i,v in [(129,31),(130,31<<5),(131,31<<10)]:struct.pack_into('<H',palette,i*2,v)
rom[0x3000:0x3200]=palette;stz(0x2121);dma(0xb000,512,0x22)
oam=bytearray(544)
for i in range(128):oam[i*4:i*4+4]=bytes((0,240,0,0));oam[512+i//4]|=1<<((i%4)*2) # x=256 hide unused
for i,(x,y,tile)in enumerate([(32,248 if a.overscan else 240,1),(96,240 if a.overscan else 232,2),(160,80,3)]):
 oam[i*4:i*4+4]=bytes((x,y,tile,0x30));oam[512+i//4]&=~(1<<((i%4)*2))
rom[0x3400:0x3400+544]=oam;stz(0x2102);stz(0x2103);dma(0xb400,544,0x04)
for row in range(64):
 for x in range(32):
  tile=1+(row%min(15,(1<<depth)-1));pal=(row//15)&7;entry=tile|(pal<<10)
  struct.pack_into('<H',rom,0x1000+(row*32+x)*2,entry)
put(0x210d,0);stz(0x210d);put(0x210e,0xff);put(0x210e,0xff)
if a.camera:put(0,a.camera&255)
else:stz(0)
put(0x2133,(1 if a.interlace else 0)|(4 if a.overscan else 0))
if a.mosaic:put(0x2106,0x31)
put(0x212c,1 if a.no_objects else 0x11);put(0x4200,1);put(0x2100,15)
label('loop');label('wait_start');emit(0xad,0x12,0x42);branch(0x10,'wait_start')
emit(0xad,0x19,0x42,0x29,0x04);branch(0xf0,'not_down');emit(0xe6,0)
label('not_down');emit(0xad,0x19,0x42,0x29,0x08);branch(0xf0,'not_up');emit(0xc6,0)
label('not_up');emit(0xa5,0);sta(0x210e)
if (a.camera>>8)&255:put(0x210e,(a.camera>>8)&255)
else:stz(0x210e)
label('wait_end');emit(0xad,0x12,0x42);branch(0x30,'wait_end');branch(0x80,'loop')
for pos,n in fix:
 delta=labels[n]-(pos+1);assert-128<=delta<=127;code[pos]=delta&255
assert len(code)<4096;rom[:len(code)]=code
rom[0x7fc0:0x7fd5]=b'SFC TALL DIAGNOSTIC'.ljust(21);rom[0x7fd5:0x7fdc]=bytes((0x20,0,7,0,2 if a.pal else 1,0,0))
for offset in (0x7fea,0x7fee,0x7ffa,0x7ffc,0x7ffe):struct.pack_into('<H',rom,offset,0x8000)
# All checksum words together contribute 0x1fe to the checksum.
checksum=(sum(rom)+0x1fe)&65535;struct.pack_into('<HH',rom,0x7fdc,checksum^65535,checksum)
out=a.out or root/'tall'/'sfc-diagnostic.sfc';out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(rom);print(out)

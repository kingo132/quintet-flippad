/* Game-specific presentation. Reads live WRAM; never patches the user's ROM.
 * Formats checked against RustyBlazer and Crysta; see THIRD_PARTY_NOTICES. */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../shared/portrait_rules.h"
static unsigned quintetProfile=0,quintetWidth=270;static bool quintetPresent=true;
enum {MAX_W=640,MAX_H=1024};
static uint32_t image[MAX_W*MAX_H];
static uint8_t depth[MAX_W*MAX_H], subdepth[MAX_W*MAX_H],obj_claimed[MAX_W*MAX_H];
static uint8_t colors[MAX_W*MAX_H], subcolors[MAX_W*MAX_H], sources[MAX_W*MAX_H];
typedef struct {
 uint16_t sx[3],sy[3],tile[3],mapbase[3],mapsize[3],fixed,palette[256],logic,output_palette[256],output_fixed;
 uint32_t windows;
 uint8_t supported,mode,main,sub,mainwin,subwin,math,select,display,wl[2],wr[2];
} Raster;
static Raster raster[224];
static int raster_valid;
static uint8_t actor_frame[0x20000];
static bool reading_actors;
static uint16_t output_color(unsigned color,unsigned brightness){
 const uint8_t*level=mul_brightness[brightness&15];
 return BUILD_PIXEL(level[color&31],level[(color>>5)&31],level[(color>>10)&31]);
}
static void QuintetBegin(){if(quintetProfile){raster_valid=0;memcpy(actor_frame,Memory.RAM,sizeof(actor_frame));}}

static void QuintetCaptureRows(){
 if(!quintetProfile||tallBusy)return;
 for(int y=IPPU.PreviousLine;y<IPPU.CurrentLine&&y<224;y++){
  if(y<0)continue;Raster*r=&raster[y];
  for(int i=0;i<3;i++){r->mapbase[i]=PPU.BG[i].SCBase;r->mapsize[i]=PPU.BG[i].SCSize;r->sx[i]=LineData[y].BG[i].HOffset;r->sy[i]=LineData[y].BG[i].VOffset-1;r->tile[i]=PPU.BG[i].NameBase;}
  r->supported=fp_supported_background(PPU.BGMode,PPU.BG[0].BGSize||PPU.BG[1].BGSize,PPU.Mosaic>1&&(PPU.BGMosaic[0]||PPU.BGMosaic[1]));
  r->mode=PPU.BGMode|(PPU.BG3Priority?8:0);r->fixed=PPU.FixedColourRed|(PPU.FixedColourGreen<<5)|(PPU.FixedColourBlue<<10);
  r->main=Memory.FillRAM[0x212c];r->sub=Memory.FillRAM[0x212d];r->mainwin=Memory.FillRAM[0x212e];r->subwin=Memory.FillRAM[0x212f];
  r->math=Memory.FillRAM[0x2131];r->select=Memory.FillRAM[0x2130];r->display=Memory.FillRAM[0x2100];
  r->windows=Memory.FillRAM[0x2123]|Memory.FillRAM[0x2124]<<8|Memory.FillRAM[0x2125]<<16;
  r->logic=Memory.FillRAM[0x212a]|Memory.FillRAM[0x212b]<<8;
  r->wl[0]=Memory.FillRAM[0x2126];r->wr[0]=Memory.FillRAM[0x2127];r->wl[1]=Memory.FillRAM[0x2128];r->wr[1]=Memory.FillRAM[0x2129];
  memcpy(r->palette,PPU.CGDATA,sizeof(r->palette));
  for(int c=0;c<256;c++)r->output_palette[c]=output_color(r->palette[c],r->display);
  r->output_fixed=output_color(r->fixed,r->display);raster_valid=1;
 }
}
static const Raster* row_state(int y){if(y<0)y=0;if(y>223)y=223;return &raster[y];}
/* The PPU scroll wraps at 1024; choose the world coordinate nearest the
   game's camera, including the fine scroll actually used by this raster. */
static int unwrap(int actual,int camera){return fp_unwrap10(actual,camera);}
static int window(const Raster*r,int layer,int x){return fp_window(r->windows,r->logic,r->wl,r->wr,layer,x);}
static int window_mode(int mode,int inside){return mode==3||(mode==2&&inside)||(mode==1&&!inside);}
static uint32_t shade(const Raster*r,int x,int color,int source,int subcolor,int subhas){
 if(r->display&128)return 0;
 int inside=window(r,5,x),clip=window_mode(r->select>>6,inside);
 bool math=source<6&&(r->math&(1<<source))&&!window_mode((r->select>>4)&3,inside);
 uint16_t out=clip?0:r->output_palette[color];
 if(math){
  uint16_t other=(r->select&2)&&subhas?r->output_palette[subcolor]:r->output_fixed;
  bool half=(r->math&64)&&(!(r->select&2)||subhas)&&!clip;
  if(r->math&128)out=half?COLOR_SUB::fn1_2(out,other):COLOR_SUB::fn(out,other);
  else if(half)out=COLOR_ADD::fn1_2(out,other);
  else if((r->display&15)==15)out=COLOR_ADD::fn(out,other);
  else{
   unsigned cap=mul_brightness[r->display&15][31];
   unsigned red=std::min(cap,unsigned((out>>11)+(other>>11))),green=std::min(cap,unsigned(((out>>6)&31)+((other>>6)&31))),blue=std::min(cap,unsigned((out&31)+(other&31)));
   out=BUILD_PIXEL(red,green,blue);
  }
 }
 // Preserve the core's exact RGB565 rounding, including half-color math.
 return ((out&0xf800)<<8)|((out&0x7e0)<<5)|((out&31)<<3);
}
static const int obj_priority[4]={2,3,6,9};
static unsigned actors_drawn;
static uint32_t hud_pixels[256*32];
static int hud_active;static bool worldRendered,soul_bottom_blank;
// Terranigma updates metatiles/camera for the next VBlank while the current
// native raster still uses the previous VRAM upload. Reuse the WRAM snapshot
// already captured at visible-frame start, for the whole presentation read.
// Otherwise a lift can fail map_ready for one frame and black out the extension.
static uint16_t word(unsigned a){const uint8_t*r=(reading_actors||quintetProfile==2)?actor_frame:Memory.RAM;return r[a]|r[a+1]<<8;}
static int char_pixel(unsigned addr,int x,int y){
 const uint16_t*v=(const uint16_t*)Memory.VRAM;unsigned a=(addr+y)&32767;
 unsigned b=v[a],c=v[(a+8)&32767],s=7-x;
 return ((b>>s)&1)|(((b>>(s+8))&1)<<1)|(((c>>s)&1)<<2)|(((c>>(s+8))&1)<<3);
}
static unsigned hardware_word(const Raster*r,int layer,int x,int y){
 unsigned tx=(x>>3)&63,ty=(y>>3)&63;
 unsigned adr=r->mapbase[layer]+(tx&31)+(ty&31)*32;
 if((r->mapsize[layer]&1)&&(tx&32))adr+=1024;
 if((r->mapsize[layer]&2)&&(ty&32))adr+=(r->mapsize[layer]&1)?2048:1024;
 return ((const uint16_t*)Memory.VRAM)[adr&32767];
}
static bool soul_hud_present(){
 const Raster*r=&raster[8];if(!(r->main&4)||PPU.BGMode!=1)return false;
 unsigned a=PPU.BG[2].SCBase;
 const uint16_t*v=(const uint16_t*)Memory.VRAM;
 return (v[(a+33)&32767]&1023)==0x24c&&(v[(a+34)&32767]&1023)==0x256&&(v[(a+96)&32767]&1023)==0x20a;
}
static bool terra_hud_present(){
 if(PPU.BGMode!=1||word(0x6be)!=0)return false;
 const Raster*r=&raster[8];if(!(r->main&4))return false;
 const uint16_t*v=(const uint16_t*)Memory.VRAM;unsigned a=PPU.BG[2].SCBase;
 // LEVEL and LIFE belong to gameplay. Inventory has another layout.
 return (v[(a+36)&32767]&1023)==0x3d&&(v[(a+37)&32767]&1023)==0x3e&&(v[(a+56)&32767]&1023)==2;
}
static void capture_hud(const uint32_t*native){
 memset(hud_pixels,0,sizeof(hud_pixels));
 for(int y=0;y<32;y++)for(int x=0;x<256;x++){
  const Raster*r=row_state(y);int sx=(r->sx[2]+x)&511,sy=(r->sy[2]+y+1)&511;
  unsigned a=hardware_word(r,2,sx,sy);int tx=sx&7,ty=sy&7;if(a&0x4000)tx=7-tx;if(a&0x8000)ty=7-ty;
  unsigned b=((const uint16_t*)Memory.VRAM)[(r->tile[2]+(a&1023)*8+ty)&32767],bit=7-tx;
  int pixel=((b>>bit)&1)|(((b>>(bit+8))&1)<<1);
  if(pixel&&(r->main&4)&&(!(r->mainwin&4)||!window(r,2,x)))hud_pixels[y*256+x]=native[y*256+x]|0xff000000;
 }
 // Terranigma's equipped-item icon is OBJ, not BG3. Require its known tile IDs
 // and its fixed HUD placement so ordinary actor sprites never become UI.
 if(quintetProfile==2)for(unsigned n=0;n<4;n++){
  const SOBJ&o=PPU.OBJ[n];if(o.Name!=0x6e&&o.Name!=0x6f&&o.Name!=0x7e&&o.Name!=0x7f)continue;
  if(o.HPos<56||o.HPos>=72||o.VPos<15||o.VPos>=32)continue;
  for(int yy=0;yy<8;yy++)for(int xx=0;xx<8;xx++){
   int x=o.HPos+xx,y=o.VPos+1+yy;if(x<0||x>=256||y<0||y>=32)continue;
   int sx=o.HFlip?7-xx:xx,sy=o.VFlip?7-yy:yy;
   if(char_pixel(((PPU.OBJNameBase>>1)+o.Name*16+((o.Name&256)?PPU.OBJNameSelect>>1:0))&32767,sx,sy))hud_pixels[y*256+x]=native[y*256+x]|0xff000000;
  }
 }
}
static bool reconstruct_native(){
 static int enabled=-1;if(enabled<0)enabled=getenv("QUINTET_RECONSTRUCT_NATIVE")?1:0;return enabled!=0;
}
static uint8_t native_replace[256*224];
static bool should_replace_native_pixel(int x,int y){
 if(reconstruct_native())return true;
 // The original Soul Blazer field reserves its final scanline as black.
 // Rebuild that line only when it is entirely blank, avoiding a floating seam.
 if(quintetProfile==1&&soul_bottom_blank&&y==223)return true;
 if(hud_active&&y<32)return true;
 // The game's eight-pixel BG3 frame belongs around the old 256x224 field.
 // Text ownership does not make that frame part of dialogue. Remove only
 // its known solid tile even while speaking; glyphs and box edges have
 // different tile words and remain the exact native pixels.
 if(quintetProfile!=2||word(0x6be)==1||(x>=8&&x<248&&y<216))return false;
 const Raster*r=row_state(y);if(!(r->main&4))return false;
 unsigned a=hardware_word(r,2,r->sx[2]+x,r->sy[2]+y+1);
 return a==0x2820;
}
static bool replace_native_pixel(int x,int y){return native_replace[y*256+x]!=0;}
static int obj_pixel(unsigned prop,int x,int y){
 unsigned tile=fp_obj_tile(prop,x,y);
 return char_pixel(((PPU.OBJNameBase>>1)+tile*16+((tile&256)?(PPU.OBJNameSelect>>1):0))&32767,x&7,y&7);
}
static int logical_layer(int hardware){
 if(quintetProfile==2)return hardware^((word(0x86a)&0x80)?1:0);
 // Soul Blazer can swap the terrain and cloud planes between BG1/BG2.
 // The live screen-map address identifies their logical WRAM maps.
 unsigned screen=PPU.BG[hardware].SCBase;
 return screen==word(0x354+(hardware^1)*4)&&screen!=word(0x354+hardware*4)?hardware^1:hardware;
}
static int camera_x(int hardware){return quintetProfile==2?word(0x81e + logical_layer(hardware)*2):word(0x33a+logical_layer(hardware)*4);}
static int camera_y(int hardware){return quintetProfile==2?word(0x822+logical_layer(hardware)*2):word(0x33c+logical_layer(hardware)*4);}
static bool map_word(int layer,int x,int y,unsigned *out){
 unsigned a;
 if(quintetProfile==2){
  int logical=logical_layer(layer),w=word(0x826+2*logical),h=word(0x82a+2*logical);
  // Alternate parallax/cloud layers are authored as repeating texture planes.
  // Their signed camera accumulators may pass through $FFFF while drifting.
  if(logical==1&&w>=256&&h>=256&&((word(0x86a)&0x40)||(!word(0x86c)&&!word(0x86e)))){x=(x%w+w)%w;y=(y%h+h)%h;}
  if(x<0||y<0||x>=w||y>=h||w<256||w>2048||h<256||h>2048)return false;
  unsigned cell=0xa000+logical*0x4000+2*((y>>4)*(w>>4)+(x>>4));if(cell+1>=0x10000)return false;
  unsigned met=word(cell)&511;a=word(0x2000+logical*0x1000+met*8+(((y>>3)&1)*2+((x>>3)&1))*2);
 }else{
  int logical=logical_layer(layer),w=word(0x342+4*logical),h=word(0x344+4*logical);
  if(logical==1&&w>=256&&h>=256){x=(x%w+w)%w;y=(y%h+h)%h;}
  if(x<0||y<0||x>=w||y>=h||w<256||w>4096||h<256||h>4096)return false;
  unsigned cell=word(0x352+logical*4)+((y>>8)*(w>>8)+(x>>8))*256+((y>>4)&15)*16+((x>>4)&15);if(cell>=0x10000)return false;
  unsigned def=word(0x35a+logical*4)+Memory.RAM[cell]*8+(((y>>3)&1)*2+((x>>3)&1))*2;if(def+1>=0x10000)return false;
  a=(word(def)&word(0x35c+logical*4))|((unsigned)Memory.RAM[0x36b+logical*4]<<8);
 }
 *out=a;return true;
}
static int bg_sample(int layer,int x,int y,int *prio,const Raster*r){
 unsigned a;if(!map_word(layer,x,y,&a))return 0;
 int tx=x&7,ty=y&7;if(a&0x4000)tx=7-tx;if(a&0x8000)ty=7-ty;
 int pix=char_pixel((r->tile[layer]+(a&1023)*16)&32767,tx,ty);if(!pix)return 0;
 *prio=(a&0x2000)?(layer?7:8):(layer?4:5);return pix+((a>>10)&7)*16;
}
/* A loading scene can publish dimensions before its VRAM ring is ready.
 * Qualify the currently enabled layers against the native ring before showing
 * anything outside the original picture. Unsupported layouts stay native. */
static bool map_ready(){
 for(int layer=0;layer<2;layer++){
  if(PPU.BG[layer].BGSize)return false;
  unsigned tested=0;bool enabled=false;
  for(int y: {8,112,216})for(int x: {8,128,248}){
   const Raster*r=row_state(y);if(r->display&128)continue;if(!r->supported)return false;
   if(!((r->main|r->sub)&(1<<layer)))continue;
   enabled=true;unsigned tile;int wx=unwrap(r->sx[layer],camera_x(layer))+x,wy=unwrap(r->sy[layer],camera_y(layer))+y+1;
   if(!map_word(layer,wx,wy,&tile))continue;
   ++tested;if(tile!=hardware_word(r,layer,r->sx[layer]+x,r->sy[layer]+y+1))return false;
  }
  if(enabled&&!tested)return false;
 }
 return true;
}
/* Sprite lists retain world coordinates before the game's 256x224 cull.
 * Static compositions use live VRAM; streaming compositions read their current
 * source sheet through the cartridge's LoROM mapping. */
static unsigned rom_byte(unsigned bank,unsigned adr){
 if(bank==0x7e||bank==0x7f)return (reading_actors?actor_frame:Memory.RAM)[((bank-0x7e)<<16)|(adr&65535)];
 unsigned bus=(bank<<16)+adr,off=quintetProfile==2?(bus&0x3fffff):(((bus&0x7f0000)>>1)|(bus&0x7fff));
 return off<Memory.CalculatedSize?Memory.ROM[off]:0;
}
static unsigned entity_byte(unsigned bank,unsigned adr){
 if(bank==0x7e||bank==0x7f)return (reading_actors?actor_frame:Memory.RAM)[((bank-0x7e)<<16)|(adr&65535)];
 return rom_byte(bank,adr);
}
static unsigned entity_word(unsigned bank,unsigned adr){return entity_byte(bank,adr)|entity_byte(bank,adr+1)<<8;}
static int source_pixel(unsigned bank,unsigned base,unsigned tile,int x,int y){
 unsigned adr=base+tile*32+y*2;unsigned bit=7-x;
 return ((rom_byte(bank,adr)>>bit)&1)|(((rom_byte(bank,adr+1)>>bit)&1)<<1)|(((rom_byte(bank,adr+16)>>bit)&1)<<2)|(((rom_byte(bank,adr+17)>>bit)&1)<<3);
}
static void actor_parts(int w,int h,int cx,int cy,int left,int top){
 actors_drawn=0;
 unsigned active[128],n=0,a=word(0x6a5);bool seen[128]={};
 while(a>=0x800&&a<0x1900&&!(a&63)&&n<68){unsigned index=(a-0x800)/64;if(seen[index])break;seen[index]=true;active[n++]=a;a=word(a+0x3c);}
 std::reverse(active,active+n);
 std::stable_sort(active,active+n,[](unsigned a,unsigned b){auto order=[](unsigned a){unsigned f=word(a+0x1a);return (f&0x4000)?0x10000:(f&0x2000)?-1:int(word(a+2));};return order(a)>order(b);});
 for(unsigned list=0;list<n;list++){
  unsigned a=active[list];
  unsigned bank=actor_frame[a+0x24],group=word(a+0x28),flip=word(a+0x1c),flags=word(a+0x16);
  if((flags&0x2000)||(word(a+0x1a)&0x80))continue;
  unsigned palette_override=0;if((flags&0x800)&&!(word(a+0x26)&1)){if((int16_t)word(a+0x26)>=0)continue;palette_override=0xe00;}
  unsigned count=entity_word(bank,group);if(!count||count>64)continue;
  int bx=word(a)-word(a+8)-cx,by=word(a+2)-word(a+10)-cy-1;
  for(unsigned k=0;k<count;k++){
   unsigned part=group+2+k*7,prop=(entity_word(bank,part+5)^flip)|palette_override;
   int size=(entity_byte(bank,part)&1)?16:8;
   int px=bx+entity_byte(bank,part+((flip&0x4000)?2:1));
   int py=by+entity_byte(bank,part+((flip&0x8000)?4:3));
   if(px+size<=0||px>=w||py+size<=0||py>=h)continue;
   int priority=obj_priority[(prop>>12)&3];
   int dynamic=word(a+0x1a)&0x8000;
   for(int y=0;y<size;y++)for(int x=0;x<size;x++){
    int dx=px+x,dy=py+y;if(dx<0||dy<0||dx>=w||dy>=h)continue;
    if(dx>=left&&dx<left+256&&dy>=top&&dy<top+224&&!replace_native_pixel(dx-left,dy-top))continue;
    int sx=(prop&0x4000)?size-1-x:x,sy=(prop&0x8000)?size-1-y:y;
    unsigned tile=(prop&511)+(sx>>3)+(sy>>3)*16;
    int pix=dynamic?source_pixel(actor_frame[a+0x2c],word(a+0x34),tile,sx&7,sy&7):obj_pixel(prop,sx,sy);
    if(!pix)continue;int at=dy*w+dx;if(!fp_claim_obj(obj_claimed,at))continue;int color=128+((prop>>9)&7)*16+pix;const Raster*r=row_state(dy-top);int win=window(r,4,dx-left);
    if((r->main&16)&&(!(r->mainwin&16)||!win)&&priority>depth[at]){colors[at]=color;sources[at]=((prop>>9)&7)<4?6:4;depth[at]=priority;}
    if((r->sub&16)&&(!(r->subwin&16)||!win)&&priority>subdepth[at]){subcolors[at]=color;subdepth[at]=priority;}
   }
   actors_drawn++;
  }
 }
}
static void terra_actor_parts(int w,int h,int cx,int cy,int left,int top){
 unsigned list[128],n=0,a=word(0xdfc);bool seen[128]={};
 while(a>=0x1000&&a<0x2000&&!(a&63)&&n<64){
  unsigned index=(a-0x1000)/64;if(seen[index])break;seen[index]=true;list[n++]=a;a=word(a+0x2c);
 }
 std::reverse(list,list+n);
 std::stable_sort(list,list+n,[](unsigned a,unsigned b){return word(a+2)>word(b+2);});
 for(unsigned i=0;i<n;i++){
  unsigned a=list[i];if(word(a+4)&0x8000)continue;
  unsigned bank=actor_frame[a+0x12],group=word(0x10000+a+0xa),flip=word(a+8)&0xc000;
  if(!group)continue;unsigned count=entity_byte(bank,group+12);if(!count||count>128)continue;
  int bx=(int)word(a)-(int16_t)word(a+0x18)-cx,by=(int)word(a+2)-(int16_t)word(a+0x1a)-cy-1;
  unsigned resource=word(0x12016+a),source=0;
  bool raw=a==0x1000&&resource<32;
  if(raw){unsigned t=0xa252+6*resource;source=Memory.ROM[t]|Memory.ROM[t+1]<<8|Memory.ROM[t+2]<<16;source&=0x3fffff;if(source+0x4000>Memory.CalculatedSize)raw=false;}
  for(unsigned k=0;k<count;k++){
   unsigned part=group+13+k*7,prop=entity_word(bank,part+5)^flip;
   int size=(entity_byte(bank,part)&1)?16:8,px=bx+entity_byte(bank,part+((flip&0x4000)?2:1)),py=by+entity_byte(bank,part+((flip&0x8000)?4:3));
   if(px+size<=0||px>=w||py+size<=0||py>=h)continue;int priority=obj_priority[(prop>>12)&3];
   for(int y=0;y<size;y++)for(int x=0;x<size;x++){
    int dx=px+x,dy=py+y;if(dx<0||dy<0||dx>=w||dy>=h)continue;
    if(dx>=left&&dx<left+256&&dy>=top&&dy<top+224&&!replace_native_pixel(dx-left,dy-top))continue;
    int sx=(prop&0x4000)?size-1-x:x,sy=(prop&0x8000)?size-1-y:y;
    unsigned tile=(prop&511)+(sx>>3)+(sy>>3)*16;int pix;
    if(raw)pix=source_pixel(source>>16,source&65535,tile,sx&7,sy&7);
    else pix=obj_pixel(prop|(word(a+8)&0x100),sx,sy);
    if(!pix)continue;int at=dy*w+dx;if(!fp_claim_obj(obj_claimed,at))continue;int color=128+((prop>>9)&7)*16+pix;const Raster*r=row_state(dy-top);int win=window(r,4,dx-left);
    if((r->main&16)&&(!(r->mainwin&16)||!win)&&priority>depth[at]){colors[at]=color;sources[at]=((prop>>9)&7)<4?6:4;depth[at]=priority;}
    if((r->sub&16)&&(!(r->subwin&16)||!win)&&priority>subdepth[at]){subcolors[at]=color;subdepth[at]=priority;}
   }
  }
 }
}
static const uint32_t *QuintetCompose(const uint32_t*native,int nw,int nh,int dw,int dh,int*ow,int*oh){
 if(nw!=256||nh!=224||dw<=0||dh<=0)return NULL;
 /* Auto-fit uses whole physical pixels. The extra columns are real world
    pixels, avoiding the 4/5-pixel cadence of nearest-neighbour 1080/256. */
 int w=dw,h=dh;
 if(w<128)w=128;if(w>MAX_W)w=MAX_W;if(h<224)h=224;if(h>MAX_H)h=MAX_H;
 int left=(w-256)/2,top=(h-224)/2;
 int map=word(quintetProfile==2?0x47e:0x314),play=quintetProfile==2?((word(0x1004)&0x8000)==0&&word(0x1000)>16):(word(0x374)!=0||word(0x376)>16);
 int world=dh>224&&play&&map<0x400&&(quintetProfile==1?soul_hud_present():word(0x6be)!=1)&&PPU.BGMode==1&&(Memory.FillRAM[0x212c]&3)&&word(quintetProfile==2?0x826:0x342)>=256&&word(quintetProfile==2?0x82a:0x344)>=256&&map_ready();
 soul_bottom_blank=world&&quintetProfile==1;
 if(soul_bottom_blank)for(int x=0;x<256;x++)if(native[223*256+x]&0xffffff){soul_bottom_blank=false;break;}
 hud_active=world&&h>224&&(quintetProfile==1?soul_hud_present():terra_hud_present());
 if(hud_active)capture_hud(native);
 for(int y=0;y<224;y++)for(int x=0;x<256;x++)native_replace[y*256+x]=should_replace_native_pixel(x,y);
 worldRendered=world&&raster_valid;
 if(worldRendered){
  int cx=(quintetProfile==2?word(0x81e):word(0x33a))-left,cy=(quintetProfile==2?word(0x822):word(0x33c))-top;
  int bounds_l=quintetProfile==2?word(0x874):0,bounds_t=quintetProfile==2?word(0x876):0,bounds_w=word(quintetProfile==2?0x878:0x342),bounds_h=word(quintetProfile==2?0x87a:0x344);
  if(quintetProfile==2&&word(0x866)>224)bounds_h-=word(0x866)-224;
  if(quintetProfile==1&&map==0&&word(0x316)==1&&bounds_h==256)bounds_h=224;
  memset(depth,0,(size_t)w*h);memset(subdepth,0,(size_t)w*h);
  memset(colors,0,(size_t)w*h);memset(subcolors,0,(size_t)w*h);memset(sources,5,(size_t)w*h);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   /* The native field is copied below; draw only the added world. */
   if(x>=left&&x<left+256&&y>=top&&y<top+224&&!replace_native_pixel(x-left,y-top))continue;
   int at=y*w+x;const Raster*r=row_state(y-top);
   for(int layer=1;layer>=0;layer--){
    if((tallLayers==1&&layer==0)||(tallLayers==2&&layer!=0))continue;
    int sx=unwrap(r->sx[layer],camera_x(layer))-left+x;
    /* Visible PPU rows are 1..224; output rows are 0..223. */
    int sy=unwrap(r->sy[layer],camera_y(layer))-top+y+1;
    int p=0,c=bg_sample(layer,sx,sy,&p,r);if(!c)continue;
    int win=window(r,layer,x-left);
    if((r->main&(1<<layer))&&(!(r->mainwin&(1<<layer))||!win)&&p>depth[at]){colors[at]=c;sources[at]=layer;depth[at]=p;}
    if((r->sub&(1<<layer))&&(!(r->subwin&(1<<layer))||!win)&&p>subdepth[at]){subcolors[at]=c;subdepth[at]=p;}
   }
  }
  memset(obj_claimed,0,(size_t)w*h);
  if(tallSprites){reading_actors=true;if(quintetProfile==1)actor_parts(w,h,cx,cy,left,top);else terra_actor_parts(w,h,cx,cy,left,top);reading_actors=false;}
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   if(x>=left&&x<left+256&&y>=top&&y<top+224&&!replace_native_pixel(x-left,y-top))continue;
   int at=y*w+x,wx=cx+x,wy=cy+y;
   image[at]=(wx<bounds_l||wy<bounds_t||wx>=bounds_w||wy>=bounds_h)?0:shade(row_state(y-top),x-left,colors[at],sources[at],subcolors[at],subdepth[at]!=0);
  }
  /* Preserve the precise native raster (HUD, dialogue, actor animation and
     scanline effects), while the surrounding world remains live. */
  for(int y=0;y<224;y++){int dy=top+y;if(dy<0||dy>=h)continue;for(int x=0;x<256;x++){int dx=left+x;if(dx>=0&&dx<w&&!replace_native_pixel(x,y))image[dy*w+dx]=native[y*256+x]&0xffffff;}}
  if(hud_active){
   int offset=0;
   for(int y=0;y<32;y++)for(int x=0;x<256;x++){
    int dx=left+x,dy=offset+y;
    if(dx>=0&&dx<w&&dy>=0&&dy<h&&(hud_pixels[y*256+x]>>24))image[dy*w+dx]=hud_pixels[y*256+x]&0xffffff;
   }
  }
 }else{
  memset(image,0,(size_t)w*h*4);
  for(int y=hud_active?32:0;y<224;y++){int dy=top+y;if(dy<0||dy>=h)continue;for(int x=0;x<256;x++){int dx=left+x;if(dx>=0&&dx<w)image[dy*w+dx]=native[y*256+x]&0xffffff;}}
 }
 *ow=w;*oh=h;return image;
}


static std::vector<uint32_t> quintetNative;
static std::vector<uint16_t> quintetPixels;
extern "C" void retro_quintet_configure(unsigned profile,unsigned width){if(quintetProfile!=profile){raster_valid=0;tallTopReady=tallBottomReady=false;tallInvalid=true;}quintetProfile=profile;quintetWidth=std::max(256u,std::min(640u,width));}
static uint32_t from565(uint16_t c){return ((c&0xf800)<<8)|((c&0x7e0)<<5)|((c&31)<<3);}
extern "C" const uint16_t *retro_quintet_compose(const uint16_t*n,unsigned width,unsigned height,size_t pitch,unsigned *outw,unsigned *outh){
 if(!quintetProfile||width!=256||height!=224||IPPU.Interlace)return nullptr;
 quintetNative.resize(256*224);for(unsigned y=0;y<224;y++)for(unsigned x=0;x<256;x++)quintetNative[y*256+x]=from565(*(const uint16_t*)((const uint8_t*)n+y*pitch+x*2));
 unsigned requested=tallSetting==1?416:tallSetting;if(!requested)requested=224;
 int w,h;const uint32_t*p=QuintetCompose(quintetNative.data(),256,224,tallSetting?quintetWidth:256,requested,&w,&h);
 quintetPixels.resize(w*h);for(int i=0;i<w*h;i++){uint32_t c=p[i];quintetPixels[i]=((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31);}
 // Preserve native pixels exactly, including their precision in RGB565.
 int left=(w-256)/2,top=(h-224)/2;for(int y=0;y<224;y++)for(int x=0;x<256;x++)if(!worldRendered||!replace_native_pixel(x,y))quintetPixels[(top+y)*w+left+x]=*(const uint16_t*)((const uint8_t*)n+y*pitch+x*2);
 *outw=w;*outh=h;return quintetPixels.data();
}

/* Read-only diagnostic: compare the complete-map path with the independent
 * native VRAM ring. No emulated registers, RAM, ROM or clocks are changed. */
extern "C" void retro_quintet_diagnostics(){
 for(int layer=0;layer<2;layer++){
  const Raster*r=&raster[112];fprintf(stderr,"layer%d camera=%d,%d scroll=%d,%d size=%d,%d mapbase=%x charbase=%x main=%x sub=%x mask=%x attr=%x\n",layer,camera_x(layer),camera_y(layer),r->sx[layer],r->sy[layer],word(0x342+layer*4),word(0x344+layer*4),r->mapbase[layer],r->tile[layer],r->main,r->sub,word(0x35c+layer*4),word(0x36a+layer*4));
  for(int y:{8,112,216})for(int x:{8,128,248}){unsigned tile=0;int wx=unwrap(r->sx[layer],camera_x(layer))+x,wy=unwrap(r->sy[layer],camera_y(layer))+y+1;bool found=map_word(layer,wx,wy,&tile);fprintf(stderr,"  %d,%d world%d,%d found%d tile%x native%x\n",x,y,wx,wy,found,tile,hardware_word(r,layer,r->sx[layer]+x,r->sy[layer]+y+1));}
 }
}
extern "C" unsigned retro_quintet_audit(unsigned *matches){
 *matches=0;if(!quintetProfile||PPU.BGMode!=1||!raster_valid)return 0;
 if(quintetProfile==1&&!soul_hud_present())return 0;
 if(quintetProfile==2&&word(0x6be)==1)return 0;
 unsigned total=0;
 for(int layer=0;layer<2;layer++)for(int y=0;y<224;y+=8)for(int x=0;x<256;x+=8){
  const Raster*r=&raster[y];if((r->display&128)||!(r->display&15)||!((r->main|r->sub)&(1<<layer)))continue;unsigned ours;
  int wx=unwrap(r->sx[layer],camera_x(layer))+x,wy=unwrap(r->sy[layer],camera_y(layer))+y+1;
  if(!map_word(layer,wx,wy,&ours))continue;
  unsigned native=hardware_word(r,layer,r->sx[layer]+x,r->sy[layer]+y+1);
  ++total;if(ours==native)++*matches;
 }
 return total;
}

extern "C" void retro_quintet_set_present(bool present){quintetPresent=present;}
extern "C" bool retro_quintet_active(){return quintetProfile!=0;}
extern "C" bool retro_quintet_should_present(){return !quintetProfile||quintetPresent;}

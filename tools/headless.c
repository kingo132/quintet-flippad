/* Small libretro validation host. Own code under the repository LICENSE. */
#include "libretro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
extern void retro_quintet_configure(unsigned,unsigned);
extern void retro_quintet_speed(float);
extern unsigned retro_quintet_audit(unsigned*);
extern void retro_quintet_set_present(bool);
extern void retro_quintet_diagnostics(void);
static const char *height, *prefix;
struct keyrange{unsigned start,end,mask;};static struct keyrange ranges[4096];static unsigned range_count;
static struct retro_core_option_v2_definition *defs;
static uint16_t frame[1088*2048];
static unsigned fw,fh,nframe,keys;static unsigned long long audio_samples,nonzero_samples;
static double monotonic(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void logger(enum retro_log_level l,const char *fmt,...){(void)l;va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
static bool env(unsigned c,void *p){
 switch(c){
 case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*)p=2;return true;
 case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:defs=((struct retro_core_options_v2*)p)->definitions;return true;
 case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:defs=((struct retro_core_options_v2_intl*)p)->us->definitions;return true;
 case RETRO_ENVIRONMENT_GET_LANGUAGE:*(unsigned*)p=RETRO_LANGUAGE_ENGLISH;return true;
 case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable *v=p;v->value=NULL;
  if(!strcmp(v->key,"snes9x_tall_height"))v->value=height;
  else if(!strcmp(v->key,"snes9x_tall_layers"))v->value=getenv("TALL_LAYERS")?getenv("TALL_LAYERS"):"all";
  else if(!strcmp(v->key,"snes9x_tall_sprites"))v->value=getenv("TALL_SPRITES")?getenv("TALL_SPRITES"):"enabled";
  else if(!strcmp(v->key,"snes9x_overscan"))v->value="auto";
  else if(!strcmp(v->key,"snes9x_mode7_hires"))v->value=getenv("TALL_HIRES")?getenv("TALL_HIRES"):"disabled";
  else if(!strcmp(v->key,"snes9x_hires_blend"))v->value="merge";
  else if(!strcmp(v->key,"snes9x_blargg"))v->value=getenv("TALL_NTSC")?"composite":"disabled";
  else if(!strcmp(v->key,"snes9x_msu1_enhanced_audio"))v->value="disabled";
  else if(defs)for(unsigned i=0;defs[i].key;i++)if(!strcmp(defs[i].key,v->key)){v->value=defs[i].default_value;break;}
  return v->value!=NULL;}
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)p=false;return true;
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)p=".";return true;
 case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((struct retro_log_callback*)p)->log=logger;return true;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return *(enum retro_pixel_format*)p==RETRO_PIXEL_FORMAT_RGB565;
 case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:return true;
 case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:*(int*)p=3;return true;
 case RETRO_ENVIRONMENT_SET_GEOMETRY:case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:return true;
 default:return false;
 }
}
static void video(const void *p,unsigned w,unsigned h,size_t stride){if(!p)return;if(w>1088||h>2048)abort();fw=w;fh=h;for(unsigned y=0;y<h;y++)memcpy(frame+y*w,(const char*)p+y*stride,w*2);}
static size_t audio(const int16_t *s,size_t n){audio_samples+=n;for(size_t i=0;i<n*2;++i)if(s[i])nonzero_samples++;return n;}
static void poll(void){}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned id){(void)d;(void)i;return p?0:(id==RETRO_DEVICE_ID_JOYPAD_MASK?keys:((keys>>id)&1));}
static void dump(unsigned idx){char path[1024];snprintf(path,sizeof(path),"%s-%u.ppm",prefix,idx);FILE*f=fopen(path,"wb");fprintf(f,"P6\n%u %u\n255\n",fw,fh);for(unsigned i=0;i<fw*fh;i++){unsigned v=frame[i];unsigned char b[3]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};fwrite(b,1,3,f);}fclose(f);}
int main(int argc,char**argv){if(argc<5){fprintf(stderr,"headless ROM HEIGHT PREFIX FRAMES [INPUT_SCRIPT]\n");return 2;}height=argv[2];prefix=argv[3];unsigned frames=atoi(argv[4]);
 retro_set_environment(env);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
 struct retro_game_info game={.path=argv[1]};if(!retro_load_game(&game))return 3;retro_set_controller_port_device(0,RETRO_DEVICE_JOYPAD);
 const char *loadpath=getenv("TALL_STATE_IN");if(loadpath){FILE*f=fopen(loadpath,"rb");if(!f)return 5;size_t sz=retro_serialize_size();void*b=calloc(1,sz);fread(b,1,sz,f);fclose(f);if(!retro_unserialize(b,sz))return 6;free(b);}
 if(getenv("QUINTET_PROFILE"))retro_quintet_configure(!strcmp(height,"disabled")?0:atoi(getenv("QUINTET_PROFILE")),270);
 if(getenv("QUINTET_SPEED"))retro_quintet_speed(atof(getenv("QUINTET_SPEED")));
 if(getenv("QUINTET_SCRIPT")){FILE*f=fopen(getenv("QUINTET_SCRIPT"),"r");if(!f)return 7;while(range_count<4096&&fscanf(f,"%u %u %x",&ranges[range_count].start,&ranges[range_count].end,&ranges[range_count].mask)==3)range_count++;fclose(f);}
 unsigned long long audited=0,matching=0;
 double total_ms=0,worst_ms=0;unsigned measured=0;
 for(nframe=0;nframe<frames;nframe++){keys=0;if(argc>5 && !strcmp(argv[5],"demo")){if(nframe%180>=100 && nframe%180<102)keys=1<<RETRO_DEVICE_ID_JOYPAD_START;if(nframe>240 && nframe<360)keys=1<<RETRO_DEVICE_ID_JOYPAD_DOWN;}
  if(argc>5 && !strcmp(argv[5],"down") && nframe>=60 && nframe<90)keys=1<<RETRO_DEVICE_ID_JOYPAD_DOWN;
  if(argc>5 && !strcmp(argv[5],"name-b")){if(nframe>=240&&nframe<300)keys=1<<RETRO_DEVICE_ID_JOYPAD_START;else if(nframe>=420&&nframe<480)keys=1<<RETRO_DEVICE_ID_JOYPAD_A;else if(nframe>=660&&(nframe-660)%61==0)keys=1<<RETRO_DEVICE_ID_JOYPAD_B;}
  if(argc>5 && !strcmp(argv[5],"name")){if(nframe%300==150&&nframe<2100)keys=1<<RETRO_DEVICE_ID_JOYPAD_START;if(nframe>=2100&&nframe%180==0)keys=1<<RETRO_DEVICE_ID_JOYPAD_A;}
  if(argc>5 && !strcmp(argv[5],"start") && nframe%300==150)keys=1<<RETRO_DEVICE_ID_JOYPAD_START;
  if(argc>5 && !strcmp(argv[5],"confirm") && nframe%120==60)keys=1<<RETRO_DEVICE_ID_JOYPAD_Y;
  for(unsigned j=0;j<range_count;j++)if(nframe>=ranges[j].start&&nframe<ranges[j].end)keys|=ranges[j].mask;
  unsigned present_every=getenv("QUINTET_PRESENT_EVERY")?atoi(getenv("QUINTET_PRESENT_EVERY")):1;if(!present_every)present_every=1;retro_quintet_set_present((nframe+1)%present_every==0||nframe==frames-1);
  double t0=monotonic();retro_run();if(getenv("QUINTET_AUDIT")){unsigned same,total=retro_quintet_audit(&same);audited+=total;matching+=same;}double ms=(monotonic()-t0)*1000;if(nframe>=60){total_ms+=ms;measured++;if(ms>worst_ms)worst_ms=ms;}if(nframe==frames-1 || (argc>5 && nframe%300==299))dump(nframe+1);
 }
 {char p[1024];snprintf(p,sizeof(p),"%s.ram",prefix);FILE*f=fopen(p,"wb");fwrite(retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),1,retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM),f);fclose(f);snprintf(p,sizeof(p),"%s.vram",prefix);f=fopen(p,"wb");fwrite(retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM),1,retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM),f);fclose(f);}
 size_t n=retro_serialize_size();void *state=calloc(1,n);if(!retro_serialize(state,n))return 4;char path[1024];snprintf(path,sizeof(path),"%s.state",prefix);FILE*f=fopen(path,"wb");fwrite(state,1,n,f);fclose(f);free(state);
 if(getenv("QUINTET_DIAG"))retro_quintet_diagnostics();
 if(getenv("QUINTET_AUDIT"))printf("map_words_matching=%llu map_words_compared=%llu\n",matching,audited);
 printf("audio_samples=%llu nonzero_samples=%llu\n",audio_samples,nonzero_samples);
 if(measured)printf("core_ms mean=%.3f max=%.3f samples=%u\n",total_ms/measured,worst_ms,measured);
 printf("{\"frames\":%u,\"width\":%u,\"height\":%u,\"state_bytes\":%zu}\n",frames,fw,fh,n);retro_unload_game();retro_deinit();return 0;
}

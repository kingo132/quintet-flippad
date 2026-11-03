/* Minimal Android libretro host for Tall verification. Snes9x LICENSE applies. */
#include <jni.h>
#include <android/bitmap.h>
#include "libretro.h"
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <android/log.h>
#include <stdarg.h>
#include <errno.h>
extern unsigned retro_tall_native_height(void);
extern void retro_quintet_configure(unsigned,unsigned);
extern void retro_quintet_speed(float);
extern void retro_quintet_set_present(bool);
static unsigned game_profile=1,world_width=270;
JNIEXPORT void JNICALL Java_local_sfc_tall_MainActivity_configureProfile(JNIEnv*e,jclass c,jint p,jint w){game_profile=p;world_width=w;retro_quintet_configure(p,w);}
extern unsigned retro_tall_double_height(void);
static unsigned width=256,height=416,canvas=1,buttons,layers;
static bool extra_sprites=true;static int sample_rate=32040;
static double frame_debt,core_fps=60;
static bool changed;
static uint16_t pixels[720*2048];
static int16_t samples[8192];
static unsigned sample_count;
static struct retro_core_option_v2_definition *defs;
static char folder[2048],rompath[2048],statepath[2100],srampath[2100];
static bool loaded;
static void logfn(enum retro_log_level l,const char*f,...){va_list a;va_start(a,f);__android_log_vprint(ANDROID_LOG_INFO,"SfcTall",f,a);va_end(a);}
static bool environment(unsigned c,void*p){
 switch(c){
 case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:*(unsigned*)p=2;return true;
 case RETRO_ENVIRONMENT_GET_LANGUAGE:*(unsigned*)p=RETRO_LANGUAGE_ENGLISH;return true;
 case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:defs=((struct retro_core_options_v2*)p)->definitions;return true;
 case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:defs=((struct retro_core_options_v2_intl*)p)->us->definitions;return true;
 case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable*v=p;static char h[16];v->value=NULL;
  if(!strcmp(v->key,"snes9x_tall_height")){snprintf(h,sizeof(h),"%u",canvas);v->value=canvas==1?"auto":canvas?h:"disabled";}
  else if(!strcmp(v->key,"snes9x_tall_layers"))v->value=layers==1?"no_bg1":layers==2?"bg1":"all";
  else if(!strcmp(v->key,"snes9x_tall_sprites"))v->value=extra_sprites?"enabled":"disabled";
  else if(!strcmp(v->key,"snes9x_overscan"))v->value="auto";
  else if(!strcmp(v->key,"snes9x_hires_blend"))v->value="merge";
  else if(!strcmp(v->key,"snes9x_msu1_enhanced_audio"))v->value="disabled";
  else if(defs)for(unsigned i=0;defs[i].key;i++)if(!strcmp(v->key,defs[i].key)){v->value=defs[i].default_value;break;}
  return v->value!=NULL;}
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)p=changed;changed=false;return true;
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)p=folder;return true;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return *(enum retro_pixel_format*)p==RETRO_PIXEL_FORMAT_RGB565;
 case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((struct retro_log_callback*)p)->log=logfn;return true;
 case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:return true;
 case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:*(int*)p=3;return true;
 case RETRO_ENVIRONMENT_SET_GEOMETRY:case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:return true;
 default:return false;
 }
}
static void video(const void*p,unsigned w,unsigned h,size_t pitch){
 if(!p||w>720||h>2048)return;
 unsigned rows=retro_tall_native_height();unsigned field_step=retro_tall_double_height()&&rows&&h==rows*2?2:1;
 width=w;height=h/field_step;
 for(unsigned y=0;y<height;y++){
  const uint16_t*a=(const uint16_t*)((const char*)p+y*field_step*pitch);
  if(field_step==1)memcpy(pixels+y*720,a,w*2);
  else{const uint16_t*b=(const uint16_t*)((const char*)a+pitch);for(unsigned x=0;x<w;x++)pixels[y*720+x]=(a[x]&b[x])+(((a[x]^b[x])&0xf7de)>>1);}
 }
}
static size_t audio(const int16_t*p,size_t n){size_t len=n*2;if(len>8192-sample_count)len=8192-sample_count;memcpy(samples+sample_count,p,len*2);sample_count+=len;return n;}
static void poll(void){}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned id){return p?0:(id==RETRO_DEVICE_ID_JOYPAD_MASK?buttons:((buttons>>id)&1));}
/* Replace in one rename; copy the previous file to a backup. A failed
   write or sync never removes the last valid save. Calls are engine-locked. */
static bool write_atomic(const char *path,const void *data,size_t n){
 char temp[2200],backup[2200],backup_temp[2200];
 snprintf(temp,sizeof(temp),"%s.tmp",path);snprintf(backup,sizeof(backup),"%s.bak",path);snprintf(backup_temp,sizeof(backup_temp),"%s.bak.tmp",path);
 FILE*f=fopen(temp,"wb");if(!f)return false;
 bool ok=fwrite(data,1,n,f)==n&&fflush(f)==0&&fsync(fileno(f))==0;
 if(fclose(f)!=0)ok=false;
 if(!ok){unlink(temp);return false;}
 FILE*old=fopen(path,"rb");
 if(old){
  FILE*bak=fopen(backup_temp,"wb");bool backup_ok=bak!=NULL;unsigned char buffer[16384];size_t got;
  while(backup_ok&&(got=fread(buffer,1,sizeof(buffer),old))>0)if(fwrite(buffer,1,got,bak)!=got)backup_ok=false;
  if(ferror(old))backup_ok=false;fclose(old);
  if(bak){if(fflush(bak)!=0||fsync(fileno(bak))!=0)backup_ok=false;if(fclose(bak)!=0)backup_ok=false;}
  if(!backup_ok||rename(backup_temp,backup)!=0){unlink(backup_temp);unlink(temp);return false;}
 }else if(errno!=ENOENT){unlink(temp);return false;}
 if(rename(temp,path)!=0){unlink(temp);return false;}
 return true;
}
static void save_ram(void){
 if(!loaded)return;void*p=retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);size_t n=retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);if(p&&n)write_atomic(srampath,p,n);
}
JNIEXPORT jboolean JNICALL Java_local_sfc_tall_MainActivity_boot(JNIEnv*e,jclass c,jstring dir,jstring path,jint h){
 if(loaded){save_ram();retro_unload_game();retro_deinit();loaded=false;}
 const char*d=(*e)->GetStringUTFChars(e,dir,0),*r=(*e)->GetStringUTFChars(e,path,0);snprintf(folder,sizeof(folder),"%s",d);snprintf(rompath,sizeof(rompath),"%s",r);(*e)->ReleaseStringUTFChars(e,dir,d);(*e)->ReleaseStringUTFChars(e,path,r);
 snprintf(statepath,sizeof(statepath),"%s.state",rompath);snprintf(srampath,sizeof(srampath),"%s.srm",rompath);canvas=h;changed=false;buttons=0;frame_debt=0;
 retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();struct retro_game_info g={.path=rompath};loaded=retro_load_game(&g);if(!loaded){retro_deinit();return false;}
 retro_set_controller_port_device(0,RETRO_DEVICE_JOYPAD);
 struct retro_system_av_info av;retro_get_system_av_info(&av);core_fps=av.timing.fps;sample_rate=(int)(av.timing.sample_rate+.5);
 void*p=retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);size_t n=retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);if(p&&n){FILE*f=fopen(srampath,"rb");if(f){fread(p,1,n,f);fclose(f);}}
 return true;
}
JNIEXPORT jint JNICALL Java_local_sfc_tall_MainActivity_step(JNIEnv*e,jclass c,jobject bitmap,jshortArray pcm,jint keys,jfloat speed){
 if(!loaded)return 0;retro_quintet_speed(speed);buttons=(unsigned)keys;sample_count=0;frame_debt+=speed;unsigned runs=(unsigned)frame_debt;frame_debt-=runs;if(runs>4)runs=4;for(unsigned i=0;i<runs;++i){retro_quintet_set_present(i+1==runs);retro_run();}retro_quintet_set_present(true);AndroidBitmapInfo info;void*dst;
 if(AndroidBitmap_getInfo(e,bitmap,&info)==0 && AndroidBitmap_lockPixels(e,bitmap,&dst)==0){for(unsigned y=0;y<height;y++)memcpy((char*)dst+y*info.stride,pixels+y*720,width*2);AndroidBitmap_unlockPixels(e,bitmap);}
 static unsigned frames=0,total_samples=0,total_nonzero=0;
 total_samples+=sample_count;for(unsigned i=0;i<sample_count;i++)total_nonzero+=samples[i]!=0;
 if(++frames%180==0){__android_log_print(ANDROID_LOG_INFO,"QuintetAudio","samples=%u nonzero=%u gameSpeed=%.2f music=1x window=180",total_samples,total_nonzero,speed);total_samples=total_nonzero=0;}
 (*e)->SetShortArrayRegion(e,pcm,0,sample_count,samples);return sample_count;
}
JNIEXPORT jint JNICALL Java_local_sfc_tall_MainActivity_dimensions(JNIEnv*e,jclass c){return (width<<16)|height;}
JNIEXPORT void JNICALL Java_local_sfc_tall_MainActivity_setCanvas(JNIEnv*e,jclass c,jint h){canvas=h;changed=true;}
JNIEXPORT jlong JNICALL Java_local_sfc_tall_MainActivity_frameNanos(JNIEnv*e,jclass c){return (jlong)(1000000000.0/core_fps);}
JNIEXPORT void JNICALL Java_local_sfc_tall_MainActivity_setLayers(JNIEnv*e,jclass c,jint v){layers=v>=0&&v<=2?v:0;changed=true;}
JNIEXPORT jboolean JNICALL Java_local_sfc_tall_MainActivity_state(JNIEnv*e,jclass c,jboolean load,jint slot){
 if(slot<0||slot>6)return false;
 if(slot==0)snprintf(statepath,sizeof(statepath),"%s.suspend.state",rompath);
 else if(slot==1)snprintf(statepath,sizeof(statepath),"%s.state",rompath);
 else snprintf(statepath,sizeof(statepath),"%s.slot%d.state",rompath,slot);
 if(!loaded)return false;size_t n=retro_serialize_size();void*p=calloc(1,n);if(!p)return false;bool ok=false;
 if(load){FILE*f=fopen(statepath,"rb");if(f){if(fread(p,1,n,f)==n&&fgetc(f)==EOF){void*before=calloc(1,n);if(before&&retro_serialize(before,n)){ok=retro_unserialize(p,n);if(!ok)retro_unserialize(before,n);}free(before);}fclose(f);}frame_debt=0;}
 else if(retro_serialize(p,n))ok=write_atomic(statepath,p,n);
 free(p);save_ram();return ok;
}
JNIEXPORT void JNICALL Java_local_sfc_tall_MainActivity_flush(JNIEnv*e,jclass c){save_ram();}
JNIEXPORT void JNICALL Java_local_sfc_tall_MainActivity_resetCore(JNIEnv*e,jclass c){if(loaded)retro_reset();}

JNIEXPORT jint JNICALL Java_local_sfc_tall_MainActivity_audioRate(JNIEnv*e,jclass c){return sample_rate;}
JNIEXPORT void JNICALL Java_local_sfc_tall_MainActivity_setSprites(JNIEnv*e,jclass c,jboolean value){extra_sprites=value;changed=true;}

JNIEXPORT jint JNICALL Java_local_sfc_tall_MainActivity_nativeHeight(JNIEnv*e,jclass c){return loaded?retro_tall_native_height():224;}

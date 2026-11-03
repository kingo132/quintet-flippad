#define main unused_headless_main
#include "headless.c"
#undef main
int main(int argc,char**argv){
 if(argc<3)return 2;height=argv[2];prefix="unused";
 retro_set_environment(env);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
 struct retro_game_info game={.path=argv[1]};if(!retro_load_game(&game))return 3;
 for(int i=0;i<180;++i)retro_run();size_t size=retro_serialize_size();void *start=calloc(1,size),*a=calloc(1,size),*b=calloc(1,size);if(!retro_serialize(start,size))return 4;
 keys=1<<RETRO_DEVICE_ID_JOYPAD_DOWN;for(int i=0;i<60;++i)retro_run();if(!retro_serialize(a,size))return 5;
 unsigned w=fw,h=fh;uint16_t* pixels=malloc(w*h*2);memcpy(pixels,frame,w*h*2);
 if(!retro_unserialize(start,size))return 6;keys=1<<RETRO_DEVICE_ID_JOYPAD_DOWN;for(int i=0;i<60;++i)retro_run();if(!retro_serialize(b,size))return 7;
 bool state_same=!memcmp(a,b,size),pixels_same=w==fw&&h==fh&&!memcmp(pixels,frame,w*h*2);
 printf("restore continuation state=%s pixels=%s dimensions=%ux%u bytes=%zu\n",state_same?"PASS":"FAIL",pixels_same?"PASS":"FAIL",w,h,size);
 free(start);free(a);free(b);free(pixels);retro_unload_game();retro_deinit();return state_same&&pixels_same?0:8;
}

#define main host_main
#include "headless.c"
#undef main
#include <unistd.h>
#include <sys/wait.h>
/* fork preserves function-pointer addresses embedded in upstream sound states. */
int main(int argc,char**argv){if(argc<5)return 2;char original[1024],base[1024];snprintf(original,sizeof(original),"%s",argv[3]);snprintf(base,sizeof(base),"%s-native",original);pid_t p=fork();if(p<0)return 2;if(p==0)return host_main(argc,argv);int st;waitpid(p,&st,0);if(st)return 3;argv[2]="disabled";argv[3]=base;return host_main(argc,argv);}

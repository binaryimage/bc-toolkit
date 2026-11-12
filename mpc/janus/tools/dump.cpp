#include "mpc_vst2_abi.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <string>
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float){ return op==1?2400:0; }
static std::string esc(const char* s){ std::string o; for(;*s;s++){ if(*s=='"'||*s=='\\') o+='\\'; o+=*s;} return o; }
int main(int argc,char**argv){
  void* h=dlopen(argv[1],RTLD_NOW); auto entry=(AEffect*(*)(audioMasterCallback))dlsym(h,"VSTPluginMain"); AEffect* e=entry(host);
  auto D=[&](int op,int i,VstIntPtr v,void*p,float o){return e->dispatcher(e,op,i,v,p,o);};
  D(effOpen,0,0,0,0); D(effSetSampleRate,0,0,0,44100); D(effSetBlockSize,0,128,0,0);
  int prog = argc>2? atoi(argv[2]) : 0; D(effSetProgram,0,prog,0,0);
  // optional overrides: idx=norm
  for(int a=3;a<argc;a++){ int i; float v; if(sscanf(argv[a],"%d=%f",&i,&v)==2) e->setParameter(e,i,v); }
  char pn[64]={0}; D(effGetProgramName,0,0,pn,0);
  printf("{\"program\":\"%s\",\"params\":[\n",esc(pn).c_str());
  for(int i=0;i<e->numParams;i++){ char n[64]={0},d[64]={0},l[64]={0}; D(effGetParamName,i,0,n,0); D(effGetParamDisplay,i,0,d,0); D(effGetParamLabel,i,0,l,0);
    printf("%s{\"i\":%d,\"name\":\"%s\",\"norm\":%.6f,\"display\":\"%s\",\"unit\":\"%s\"}\n", i?",":"", i, esc(n).c_str(), e->getParameter(e,i), esc(d).c_str(), esc(l).c_str()); }
  printf("]}\n"); D(effClose,0,0,0,0); }

// torture.cpp - drives Maggot through every mangle button, the arp and WRECK while a chord is held.
#include "mpc_vst2_abi.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cstdlib>
static VstTimeInfo ti;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float){
  if(op==1) return 2400;
  if(op==7){ return (VstIntPtr)&ti; }
  return 0; }
int main(int argc,char**argv){
  void* h=dlopen(argv[1],RTLD_NOW); if(!h){printf("%s\n",dlerror());return 1;}
  auto entry=(AEffect*(*)(audioMasterCallback))dlsym(h,"VSTPluginMain"); AEffect* e=entry(host);
  auto D=[&](int op,int i,VstIntPtr v,void*p,float o){return e->dispatcher(e,op,i,v,p,o);};
  const int sr=44100, blk=atoi(argc>3?argv[3]:"128");
  D(effOpen,0,0,0,0); D(effSetSampleRate,0,0,0,sr); D(effSetBlockSize,0,blk,0,0); D(effMainsChanged,0,1,0,0);
  memset(&ti,0,sizeof ti); ti.sampleRate=sr; ti.tempo=128; ti.flags=0x400|0x2|0x200; // tempo valid, playing, ppq valid
  auto idx=[&](const char* name){ for(int i=0;i<e->numParams;i++){ char n[64]={0}; D(effGetParamName,i,0,n,0); if(!strcmp(n,name)) return i;} printf("no param %s\n",name); exit(1); };
  auto setN=[&](const char* name,float norm){ e->setParameter(e,idx(name),norm); };
  // arp on, mode Converge-ish, ratchets, jumps, mutate
  setN("Arp On",1); setN("Arp Mode",3/7.f); setN("Ratchet Prob",0.5f); setN("Oct Jump",0.3f); setN("Mutate",0.5f); setN("Note Prob",0.85f);
  setN("Res Mix",0.4f); setN("Noise",0.2f); setN("Delay Mix",0.3f); setN("Rev Mix",0.3f);
  const double secs=12; const int N=(int)(secs*sr);
  std::vector<float> L(N),R(N); float bl[4096],br[4096]; float* outs[2]={bl,br};
  struct Ev{double t; const char* name; float v;};
  std::vector<Ev> autom={{2.0,"Tape Stop",1},{3.0,"Tape Stop",0},{4.0,"Stutter",1},{5.0,"Stutter",0},{5.5,"Reverse",1},{6.5,"Reverse",0},
    {7.0,"Freeze",1},{8.0,"Freeze",0},{8.5,"Glitch",1},{9.5,"WRECK",1},{10.5,"WRECK",0},{10.5,"Glitch",0},{11.0,"Arp Hold",1}};
  size_t ai=0; bool chordOn=false, chordOff=false; std::vector<double> bt; double ppq=0;
  for(int pos=0;pos<N;pos+=blk){
    int n=std::min(blk,N-pos); double t=(double)pos/sr;
    while(ai<autom.size() && autom[ai].t<=t){ setN(autom[ai].name,autom[ai].v); ai++; }
    ti.ppqPos=ppq; ti.samplePos=pos;
    VstMidiEvent me[8]; int ne=0;
    auto note=[&](int st,int nn,int vv){ memset(&me[ne],0,sizeof me[ne]); me[ne].type=kVstMidiType; me[ne].byteSize=sizeof me[ne]; me[ne].midiData[0]=st; me[ne].midiData[1]=nn; me[ne].midiData[2]=vv; ne++; };
    if(!chordOn && t>=0.1){ chordOn=true; note(0x90,48,100); note(0x90,51,100); note(0x90,55,100); note(0x90,58,100);}
    if(!chordOff && t>=11.5){ chordOff=true; note(0x80,48,0); note(0x80,51,0); note(0x80,55,0); note(0x80,58,0);}
    if(ne){ std::vector<char> mem(sizeof(VstEvents)+sizeof(void*)*(ne+2)); VstEvents* ve=(VstEvents*)mem.data(); ve->numEvents=ne; for(int k=0;k<ne;k++) ve->events[k]=(VstEvent*)&me[k]; D(effProcessEvents,0,0,ve,0); }
    memset(bl,0,sizeof bl); memset(br,0,sizeof br);
    auto a=std::chrono::steady_clock::now(); e->processReplacing(e,nullptr,outs,n); auto b=std::chrono::steady_clock::now();
    bt.push_back(std::chrono::duration<double,std::micro>(b-a).count());
    for(int i=0;i<n;i++){L[pos+i]=bl[i];R[pos+i]=br[i];}
    ppq += n*128.0/60.0/sr;
  }
  float pk=0; int bad=0, clicks=0; float prev=0;
  for(int i=0;i<N;i++){ if(!std::isfinite(L[i])||!std::isfinite(R[i])) bad++; pk=std::max(pk,std::max(fabsf(L[i]),fabsf(R[i]))); if(fabsf(L[i]-prev)>0.6f) clicks++; prev=L[i]; }
  std::sort(bt.begin(),bt.end()); double budget=1e6*blk/sr;
  printf("block %d: peak %.1f dB, NaN %d, big jumps %d, cpu median %.1f%% p99 %.1f%% max %.1f%%\n",blk,20*log10(pk+1e-9),bad,clicks,100*bt[bt.size()/2]/budget,100*bt[(size_t)(bt.size()*0.99)]/budget,100*bt.back()/budget);
  FILE* f=fopen("/tmp/torture.raw","wb"); for(int i=0;i<N;i++){ float s[2]={L[i],R[i]}; fwrite(s,4,2,f);} fclose(f);
  D(effMainsChanged,0,0,0,0); D(effClose,0,0,0,0);
}

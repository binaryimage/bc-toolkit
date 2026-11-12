// render.cpp - loads a plugin .so and renders each built-in program to a WAV:
// notes play a short riff, then the mod wheel sweeps 0 -> 127 -> 0 (scene morph).
#include "mpc_vst2_abi.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <chrono>
#include <string>
#include <algorithm>
#include <cstdlib>
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void* ptr, float) {
    if (op == 1) return 2400; // version
    if (op == 7) { // audioMasterGetTime
        static VstTimeInfo ti; memset(&ti,0,sizeof ti); ti.sampleRate=44100; ti.tempo=120; ti.flags=0x400|0x2; return (VstIntPtr)&ti; }
    return 0; }
struct Ev { int frame; unsigned char a,b,c; };
static void wav(const char* path, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f=fopen(path,"wb"); int n=L.size(); int data=n*4; int sr=44100;
    auto w32=[&](int v){fwrite(&v,4,1,f);}; auto w16=[&](short v){fwrite(&v,2,1,f);};
    fwrite("RIFF",1,4,f); w32(36+data); fwrite("WAVEfmt ",1,8,f); w32(16); w16(1); w16(2); w32(sr); w32(sr*4); w16(4); w16(16);
    fwrite("data",1,4,f); w32(data);
    for(int i=0;i<n;i++){ for(float x: {L[i],R[i]}){ float c=x>1?1:(x<-1?-1:x); w16((short)lrintf(c*32767)); } }
    fclose(f); }
int main(int argc, char** argv) {
    if (argc < 3) { printf("render <plugin.so> <outdir> [seconds]\n"); return 1; }
    void* h = dlopen(argv[1], RTLD_NOW); if(!h){printf("%s\n",dlerror());return 1;}
    auto entry=(AEffect*(*)(audioMasterCallback))dlsym(h,"VSTPluginMain");
    AEffect* e=entry(host);
    auto D=[&](int op,int idx,VstIntPtr v,void*p,float o){return e->dispatcher(e,op,idx,v,p,o);};
    const int sr=44100, blk=128; const double secs = argc>3? atof(argv[3]) : 8.0;
    D(effOpen,0,0,0,0); D(effSetSampleRate,0,0,0,sr); D(effSetBlockSize,0,blk,0,0); D(effMainsChanged,0,1,0,0);
    int nprog = e->numPrograms;
    for (int p=0;p<nprog;p++){
        char name[64]={0}; D(effSetProgram,0,p,0,0); D(effGetProgramName,0,0,name,0);
        if(getenv("STRESS")){ for(int k=0;k<e->numParams;k++){} int fx[]={atoi(getenv("STRESS"))}; (void)fx; for(int k: {e->numParams-16+2, e->numParams-16+6, e->numParams-16+10, e->numParams-16+13}) e->setParameter(e,k,0.7f); }
        D(effMainsChanged,0,0,0,0); D(effMainsChanged,0,1,0,0);
        // events: riff in C minor-ish phrygian over first half, held chord second half w/ wheel sweep
        std::vector<Ev> ev; int N=(int)(secs*sr);
        int riff[]={36,36,48,37,36,43,42,36}; double step=0.25;
        for(int i=0;i<8;i++){ int t=(int)(i*step*sr); ev.push_back({t,0x90,(unsigned char)riff[i],(unsigned char)(i%2?90:120)}); ev.push_back({t+(int)(0.18*sr),0x80,(unsigned char)riff[i],0}); }
        int t2=(int)(2.2*sr); std::vector<int> chord={48,55,60,63}; if(getenv("STRESS")) chord={36,43,48,51,55,58,60,63,67,70};
        for(int c: chord) ev.push_back({t2,0x90,(unsigned char)c,100});
        int tw0=(int)(2.6*sr), tw1=(int)((secs-1.6)*sr);
        for(int t=tw0;t<tw1;t+=sr/50){ double ph=(double)(t-tw0)/(tw1-tw0); int v=(int)lround(127*(ph<0.5?ph*2:2-ph*2)); ev.push_back({t,0xB0,1,(unsigned char)v}); }
        for(int c: chord) ev.push_back({(int)((secs-1.2)*sr),0x80,(unsigned char)c,0});
        std::sort(ev.begin(),ev.end(),[](const Ev&a,const Ev&b){return a.frame<b.frame;});
        std::vector<float> L(N),R(N); float bufL[blk],bufR[blk]; float* outs[2]={bufL,bufR};
        size_t ei=0; double cpu=0, worst=0;
        for(int pos=0;pos<N;pos+=blk){
            int n=std::min(blk,N-pos);
            std::vector<VstMidiEvent> me; while(ei<ev.size() && ev[ei].frame<pos+n){ VstMidiEvent m; memset(&m,0,sizeof m); m.type=kVstMidiType; m.byteSize=sizeof m; m.deltaFrames=ev[ei].frame-pos; m.midiData[0]=ev[ei].a; m.midiData[1]=ev[ei].b; m.midiData[2]=ev[ei].c; me.push_back(m); ei++; }
            std::vector<char> mem(sizeof(VstEvents)+sizeof(void*)*(me.size()+2)); VstEvents* ve=(VstEvents*)mem.data(); ve->numEvents=me.size(); for(size_t k=0;k<me.size();k++) ve->events[k]=(VstEvent*)&me[k];
            if(!me.empty()) D(effProcessEvents,0,0,ve,0);
            memset(bufL,0,sizeof bufL); memset(bufR,0,sizeof bufR);
            auto a=std::chrono::steady_clock::now(); e->processReplacing(e,nullptr,outs,n); auto b=std::chrono::steady_clock::now();
            double us=std::chrono::duration<double,std::micro>(b-a).count(); cpu+=us; if(us>worst){worst=us; if(getenv("WB")) fprintf(stderr,"  worst now block %d (t=%.2fs) %.0fus\n",pos/blk,(double)pos/sr,us);}
            for(int i=0;i<n;i++){L[pos+i]=bufL[i];R[pos+i]=bufR[i];}
        }
        float pk=0; double rms=0; bool bad=false; for(int i=0;i<N;i++){ if(!std::isfinite(L[i])||!std::isfinite(R[i])) bad=true; pk=std::max(pk,std::max(fabsf(L[i]),fabsf(R[i]))); rms+=L[i]*L[i]; }
        rms=sqrt(rms/N);
        double blockUs = 1e6*blk/sr; int nb=(N+blk-1)/blk;
        printf("%2d %-18s peak %6.1f dB  rms %6.1f dB  cpu avg %.2f%% worst %.2f%% %s\n",p,name,20*log10(pk+1e-9),20*log10(rms+1e-9),100*cpu/nb/blockUs,100*worst/blockUs,bad?"NAN!":"");
        std::string fn=std::string(argv[2])+"/"+std::to_string(p)+"_"+name+".wav"; for(auto& ch:fn) if(ch==' ') ch='_';
        wav(fn.c_str(),L,R);
    }
    D(effMainsChanged,0,0,0,0); D(effClose,0,0,0,0);
}

// Prüf-Plug-in: AUv2-Effekt "Gain" (Typ aufx, Unterart gain, Hersteller Clok) als .component, nur zum Testen
// des Komponenten-Registers (Info.plist -> dlopen -> Fabrikfunktion -> Lookup).
#include <AudioToolbox/AudioToolbox.h>
#include <stdlib.h>
#include <string.h>

typedef struct { AudioComponentPlugInInterface iface; AudioComponentInstance inst; float gain; AURenderCallbackStruct cb; AudioStreamBasicDescription fmt; int init; } G;

static OSStatus g_open(void *s, AudioComponentInstance i) { ((G *)s)->inst = i; return 0; }
static OSStatus g_close(void *s) { free(s); return 0; }
static OSStatus g_init(void *s) { ((G *)s)->init = 1; return 0; }
static OSStatus g_uninit(void *s) { ((G *)s)->init = 0; return 0; }
static OSStatus g_getinfo(void *s, AudioUnitPropertyID p, AudioUnitScope sc, AudioUnitElement e, UInt32 *sz, Boolean *w) {
  if (p == kAudioUnitProperty_StreamFormat) { *sz = sizeof(AudioStreamBasicDescription); *w = 1; return 0; }
  if (p == kAudioUnitProperty_SetRenderCallback) { *sz = sizeof(AURenderCallbackStruct); *w = 1; return 0; }
  return kAudioUnitErr_InvalidProperty;
}
static OSStatus g_get(void *s, AudioUnitPropertyID p, AudioUnitScope sc, AudioUnitElement e, void *d, UInt32 *sz) {
  G *g = s;
  if (p == kAudioUnitProperty_StreamFormat) { memcpy(d, &g->fmt, sizeof g->fmt); *sz = sizeof g->fmt; return 0; }
  return kAudioUnitErr_InvalidProperty;
}
static OSStatus g_set(void *s, AudioUnitPropertyID p, AudioUnitScope sc, AudioUnitElement e, const void *d, UInt32 sz) {
  G *g = s;
  if (p == kAudioUnitProperty_StreamFormat) { memcpy(&g->fmt, d, sizeof g->fmt); return 0; }
  if (p == kAudioUnitProperty_SetRenderCallback) { memcpy(&g->cb, d, sizeof g->cb); return 0; }
  return kAudioUnitErr_InvalidProperty;
}
static OSStatus g_getp(void *s, AudioUnitParameterID p, AudioUnitScope sc, AudioUnitElement e, AudioUnitParameterValue *v) { *v = ((G *)s)->gain; return 0; }
static OSStatus g_setp(void *s, AudioUnitParameterID p, AudioUnitScope sc, AudioUnitElement e, AudioUnitParameterValue v, UInt32 o) { ((G *)s)->gain = v; return 0; }
static OSStatus g_render(void *s, AudioUnitRenderActionFlags *fl, const AudioTimeStamp *ts, UInt32 bus, UInt32 n, AudioBufferList *io) {
  G *g = s;
  if (!g->init) return kAudioUnitErr_Uninitialized;
  OSStatus r = g->cb.inputProc ? g->cb.inputProc(g->cb.inputProcRefCon, fl, ts, 0, n, io) : kAudioUnitErr_NoConnection;
  if (r) return r;
  for (UInt32 b = 0; b < io->mNumberBuffers; b++) { float *f = io->mBuffers[b].mData; for (UInt32 i = 0; i < io->mBuffers[b].mDataByteSize / 4; i++) f[i] *= g->gain; }
  return 0;
}
static AudioComponentMethod g_lookup(SInt16 sel) {
  switch (sel) {
    case kAudioUnitInitializeSelect: return (AudioComponentMethod)g_init;
    case kAudioUnitUninitializeSelect: return (AudioComponentMethod)g_uninit;
    case kAudioUnitGetPropertyInfoSelect: return (AudioComponentMethod)g_getinfo;
    case kAudioUnitGetPropertySelect: return (AudioComponentMethod)g_get;
    case kAudioUnitSetPropertySelect: return (AudioComponentMethod)g_set;
    case kAudioUnitGetParameterSelect: return (AudioComponentMethod)g_getp;
    case kAudioUnitSetParameterSelect: return (AudioComponentMethod)g_setp;
    case kAudioUnitRenderSelect: return (AudioComponentMethod)g_render;
    default: return NULL;
  }
}
AudioComponentPlugInInterface *CtGainFactory(const AudioComponentDescription *d) {
  G *g = calloc(1, sizeof *g);
  g->iface.Open = g_open; g->iface.Close = g_close; g->iface.Lookup = g_lookup;
  g->gain = 1;
  g->fmt = (AudioStreamBasicDescription){ 48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0 };
  return &g->iface;
}

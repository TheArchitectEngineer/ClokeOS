// Prüfprogramm für die AudioToolbox-Teilmenge, nur öffentliche API.
//   attest self              Dateien (WAV/AIFF/CAF), Wandler, Komponentenregister, Ausgabe-Unit nach CLOKE_CA_DEVICE; kein Gerät nötig
//   attest play [sekunden]   Sinus erst über ExtAudioFile (WAV schreiben, lesen), dann über den Ausgabe-Unit abspielen
#include <AudioToolbox/AudioToolbox.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define OK(c, ...) do { if (!(c)) { fails++; printf("FEHLER %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define CK(x) do { OSStatus s_ = (x); if (s_) { fails++; printf("FEHLER %s:%d: %s -> %d\n", __FILE__, __LINE__, #x, (int)s_); } } while (0)

static AudioStreamBasicDescription pcm(double r, UInt32 ch, UInt32 bits, int flt, int be, int ni) {
  AudioStreamBasicDescription d = { r, kAudioFormatLinearPCM, (flt ? kAudioFormatFlagIsFloat : kAudioFormatFlagIsSignedInteger) | kAudioFormatFlagIsPacked |
    (be ? kAudioFormatFlagIsBigEndian : 0) | (ni ? kAudioFormatFlagIsNonInterleaved : 0), 0, 1, 0, ch, bits, 0 };
  d.mBytesPerFrame = d.mBytesPerPacket = (bits / 8) * (ni ? 1 : ch);
  return d;
}
static CFURLRef url(const char *p) { return CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, (const UInt8 *)p, strlen(p), false); }

// 440 Hz Sinus in Float32 interleaved stereo, L = 0.5 sin, R = 0.25 sin
static void gen(float *b, UInt32 n, UInt64 start, double rate) {
  for (UInt32 i = 0; i < n; i++) { double s = sin(2 * M_PI * 440.0 * (double)(start + i) / rate); b[2 * i] = (float)(0.5 * s); b[2 * i + 1] = (float)(0.25 * s); }
}
static OSStatus write_file(const char *path, AudioFileTypeID t, AudioStreamBasicDescription ff, double secs) {
  ExtAudioFileRef e; OSStatus r;
  CFURLRef u = url(path); unlink(path);
  if ((r = ExtAudioFileCreateWithURL(u, t, &ff, NULL, kAudioFileFlags_EraseFile, &e))) return r;
  AudioStreamBasicDescription cf = pcm(48000, 2, 32, 1, 0, 0);
  CK(ExtAudioFileSetProperty(e, kExtAudioFileProperty_ClientDataFormat, sizeof cf, &cf));
  UInt64 total = (UInt64)(48000 * secs), done = 0; float *b = malloc(1000 * 8);
  while (done < total) {
    UInt32 n = total - done < 1000 ? (UInt32)(total - done) : 1000; gen(b, n, done, 48000);
    AudioBufferList abl = { 1, { { 2, n * 8, b } } };
    if ((r = ExtAudioFileWrite(e, n, &abl))) break;
    done += n;
  }
  free(b);
  OSStatus r2 = ExtAudioFileDispose(e); CFRelease(u);
  return r ? r : r2;
}
// Liest alles im Clientformat cf, liefert Frames und Spitzenwert/Nulldurchgänge des 1. Kanals
static OSStatus read_all(const char *path, AudioStreamBasicDescription cf, UInt64 *frames, double *peak, double *freq, AudioStreamBasicDescription *fileFmt, SInt64 *flen) {
  ExtAudioFileRef e; OSStatus r; CFURLRef u = url(path);
  if ((r = ExtAudioFileOpenURL(u, &e))) { CFRelease(u); return r; }
  UInt32 sz = sizeof *fileFmt; CK(ExtAudioFileGetProperty(e, kExtAudioFileProperty_FileDataFormat, &sz, fileFmt));
  sz = sizeof *flen; CK(ExtAudioFileGetProperty(e, kExtAudioFileProperty_FileLengthFrames, &sz, flen));
  CK(ExtAudioFileSetProperty(e, kExtAudioFileProperty_ClientDataFormat, sizeof cf, &cf));
  UInt32 bpf = cf.mBytesPerFrame, ch = cf.mChannelsPerFrame, nb = (cf.mFormatFlags & kAudioFormatFlagIsNonInterleaved) ? ch : 1;
  char *buf[8]; for (UInt32 k = 0; k < nb; k++) buf[k] = malloc(700 * bpf * (nb == 1 ? 1 : 1) * (nb == 1 ? 1 : 1));
  *frames = 0; *peak = 0; double prev = 0; UInt64 zc = 0, firstzc = 0, lastzc = 0;
  for (;;) {
    AudioBufferList *abl = alloca(sizeof(AudioBufferList) + 8 * sizeof(AudioBuffer)); abl->mNumberBuffers = nb;
    for (UInt32 k = 0; k < nb; k++) abl->mBuffers[k] = (AudioBuffer){ nb == 1 ? ch : 1, 700 * bpf, buf[k] };
    UInt32 n = 700;
    if ((r = ExtAudioFileRead(e, &n, abl))) break;
    if (!n) break;
    for (UInt32 i = 0; i < n; i++) {
      double v;
      if (cf.mFormatFlags & kAudioFormatFlagIsFloat) v = ((float *)buf[0])[i * (nb == 1 ? ch : 1)];
      else v = ((short *)buf[0])[i * (nb == 1 ? ch : 1)] / 32768.0;
      if (fabs(v) > *peak) *peak = fabs(v);
      if (prev < 0 && v >= 0) { zc++; if (!firstzc) firstzc = *frames + i; lastzc = *frames + i; }
      prev = v;
    }
    *frames += n;
  }
  *freq = zc > 1 ? (double)(zc - 1) * cf.mSampleRate / (double)(lastzc - firstzc) : 0;
  for (UInt32 k = 0; k < nb; k++) free(buf[k]);
  ExtAudioFileDispose(e); CFRelease(u);
  return r;
}

static void test_files(void) {
  struct { const char *name; AudioFileTypeID t; AudioStreamBasicDescription f; double tol; } cases[] = {
    { "/tmp/at-16.wav", kAudioFileWAVEType, pcm(48000, 2, 16, 0, 0, 0), 1.0 / 16384 },
    { "/tmp/at-24.wav", kAudioFileWAVEType, pcm(48000, 2, 24, 0, 0, 0), 1e-5 },
    { "/tmp/at-f32.wav", kAudioFileWAVEType, pcm(48000, 2, 32, 1, 0, 0), 1e-7 },
    { "/tmp/at-16.aif", kAudioFileAIFFType, pcm(48000, 2, 16, 0, 1, 0), 1.0 / 16384 },
    { "/tmp/at-24.aif", kAudioFileAIFFType, pcm(44100, 2, 24, 0, 1, 0), 1e-5 },
    { "/tmp/at-16.aifc", kAudioFileAIFCType, pcm(48000, 2, 16, 0, 0, 0), 1.0 / 16384 },
    { "/tmp/at-16.caf", kAudioFileCAFType, pcm(48000, 2, 16, 0, 0, 0), 1.0 / 16384 },
    { "/tmp/at-f32.caf", kAudioFileCAFType, pcm(48000, 2, 32, 1, 0, 0), 1e-7 },
  };
  for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
    OSStatus r = write_file(cases[i].name, cases[i].t, cases[i].f, 0.5);
    OK(!r, "%s schreiben: %d", cases[i].name, (int)r);
    UInt64 fr; double pk, fq; AudioStreamBasicDescription ff; SInt64 fl;
    r = read_all(cases[i].name, pcm(cases[i].f.mSampleRate, 2, 32, 1, 0, 0), &fr, &pk, &fq, &ff, &fl);
    // Ziel: 0.5 Spitze; die Datei hat die Dateirate, Länge 0.5 s bei 48 kHz (die Schreibrate war 48 kHz)
    double expectFrames = cases[i].f.mSampleRate * 0.5;
    OK(!r, "%s lesen: %d", cases[i].name, (int)r);
    OK(ff.mSampleRate == cases[i].f.mSampleRate && ff.mBitsPerChannel == cases[i].f.mBitsPerChannel && ff.mChannelsPerFrame == 2 &&
       (ff.mFormatFlags & (kAudioFormatFlagIsFloat | kAudioFormatFlagIsBigEndian)) == (cases[i].f.mFormatFlags & (kAudioFormatFlagIsFloat | kAudioFormatFlagIsBigEndian)),
       "%s Dateiformat: %g Hz %u bit flags %x", cases[i].name, ff.mSampleRate, (unsigned)ff.mBitsPerChannel, (unsigned)ff.mFormatFlags);
    OK(fabs((double)fr - expectFrames) <= expectFrames * 0.001 + 2 && fl == (SInt64)fr, "%s Länge %llu / %lld, erwartet %.0f", cases[i].name, fr, fl, expectFrames);
    OK(fabs(pk - 0.5) < 0.002 + cases[i].tol, "%s Spitze %.6f", cases[i].name, pk);
    OK(fabs(fq - 440) < 1.5, "%s Frequenz %.2f", cases[i].name, fq);
    printf("%-18s %llu Frames, Spitze %.5f, %.2f Hz, Datei %g Hz %u Bit\n", cases[i].name, fr, pk, fq, ff.mSampleRate, (unsigned)ff.mBitsPerChannel);
  }
  // Wandler: 48 kHz Float stereo (WAV 16) -> 44.1 kHz Mono int16 und -> 96 kHz nicht verschachtelt Float
  UInt64 fr; double pk, fq; AudioStreamBasicDescription ff; SInt64 fl;
  CK(read_all("/tmp/at-16.wav", pcm(44100, 1, 16, 0, 0, 0), &fr, &pk, &fq, &ff, &fl));
  OK(fabs((double)fr - 22050) < 8 && fabs(fq - 440) < 2, "44.1k mono: %llu Frames %.2f Hz", fr, fq);
  printf("-> 44100 Hz Mono s16: %llu Frames (erwartet 22050), Spitze %.4f, %.2f Hz\n", fr, pk, fq);
  CK(read_all("/tmp/at-16.wav", pcm(96000, 2, 32, 1, 0, 1), &fr, &pk, &fq, &ff, &fl));
  OK(fabs((double)fr - 48000) < 8 && fabs(fq - 440) < 2, "96k nonint: %llu Frames %.2f Hz", fr, fq);
  printf("-> 96000 Hz Float nicht verschachtelt: %llu Frames (erwartet 48000), Spitze %.4f, %.2f Hz\n", fr, pk, fq);
  // Direkter AudioConverter: int16 BE stereo -> float, ConvertBuffer
  AudioStreamBasicDescription s = pcm(48000, 2, 16, 0, 1, 0), d = pcm(48000, 2, 32, 1, 0, 0); AudioConverterRef c;
  CK(AudioConverterNew(&s, &d, &c));
  uint8_t in[8] = { 0x40, 0x00, 0xC0, 0x00, 0x7F, 0xFF, 0x80, 0x00 }; float out[4]; UInt32 osz = sizeof out;
  CK(AudioConverterConvertBuffer(c, sizeof in, in, &osz, out));
  OK(osz == 16 && out[0] == 0.5f && out[1] == -0.5f && fabsf(out[2] - 32767.0f / 32768) < 1e-9 && out[3] == -1.0f, "ConvertBuffer: %g %g %g %g", out[0], out[1], out[2], out[3]);
  AudioConverterDispose(c);
  AudioStreamBasicDescription aac = { 44100, kAudioFormatMPEG4AAC, 0, 0, 1024, 0, 2, 0, 0 };
  OK(AudioConverterNew(&aac, &d, &c) == kAudioConverterErr_FormatNotSupported, "AAC muss abgelehnt werden");
}

// --- Komponentenregister und AUv2-Plug-in -------------------------------------------------------
static OSStatus feed(void *ref, AudioUnitRenderActionFlags *fl, const AudioTimeStamp *ts, UInt32 bus, UInt32 n, AudioBufferList *io) {
  for (UInt32 b = 0; b < io->mNumberBuffers; b++) { float *f = io->mBuffers[b].mData; for (UInt32 i = 0; i < n; i++) f[i] = 0.5f; }
  return 0;
}
static void test_components(void) {
  AudioComponentDescription want = { kAudioUnitType_Effect, 'gain', 'Clok', 0, 0 };
  AudioComponent c = AudioComponentFindNext(NULL, &want);
  if (!c) { printf("Gain-Plug-in nicht gefunden (CLOKE_AU_PATH gesetzt?), übersprungen\n"); fails++; return; }
  AudioComponentDescription got; CK(AudioComponentGetDescription(c, &got));
  CFStringRef name; CK(AudioComponentCopyName(c, &name)); char nb[128]; CFStringGetCString(name, nb, sizeof nb, kCFStringEncodingUTF8);
  UInt32 ver; CK(AudioComponentGetVersion(c, &ver));
  printf("Komponente: '%s' Version 0x%x\n", nb, ver);
  OK(!strcmp(nb, "Clok: Test Gain") && ver == 0x10000, "Name/Version");
  AudioUnit u; CK(AudioComponentInstanceNew(c, &u));
  AURenderCallbackStruct cb = { feed, NULL }; CK(AudioUnitSetProperty(u, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb));
  CK(AudioUnitSetParameter(u, 0, kAudioUnitScope_Global, 0, 0.5f, 0));
  AudioUnitParameterValue pv = 0; CK(AudioUnitGetParameter(u, 0, kAudioUnitScope_Global, 0, &pv)); OK(pv == 0.5f, "Parameter %g", pv);
  float buf[64]; AudioBufferList abl = { 1, { { 1, sizeof buf, buf } } }; AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts = { 0 };
  OK(AudioUnitRender(u, &fl, &ts, 0, 64, &abl) == kAudioUnitErr_Uninitialized, "Render vor Initialize");
  CK(AudioUnitInitialize(u));
  CK(AudioUnitRender(u, &fl, &ts, 0, 64, &abl));
  OK(buf[0] == 0.25f && buf[63] == 0.25f, "Render: %g", buf[0]);
  UInt32 sz; Boolean w; CK(AudioUnitGetPropertyInfo(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &sz, &w));
  OK(sz == sizeof(AudioStreamBasicDescription) && w, "PropertyInfo");
  OK(AudioUnitGetPropertyInfo(u, kAudioUnitProperty_ClassInfo, 0, 0, &sz, &w) == kAudioUnitErr_InvalidProperty, "unbekannte Eigenschaft");
  CK(AudioUnitUninitialize(u)); CK(AudioComponentInstanceDispose(u));
  printf("AUv2-Plug-in: Parameter, Render, Eigenschaften ok\n");
  // Zählen und AudioComponentRegister
  AudioComponentDescription all = { kAudioUnitType_Output, 0, 0, 0, 0 };
  int n = 0; for (AudioComponent x = AudioComponentFindNext(NULL, &all); x; x = AudioComponentFindNext(x, &all)) n++;
  OK(n >= 3, "Ausgabekomponenten: %d", n);
}

// --- Ausgabe-Unit ---------------------------------------------------------------------------------
typedef struct { ExtAudioFileRef ext; UInt64 frames; UInt32 bpf; } Play;
static OSStatus pcb(void *ref, AudioUnitRenderActionFlags *fl, const AudioTimeStamp *ts, UInt32 bus, UInt32 n, AudioBufferList *io) {
  Play *p = ref; UInt32 got = n;
  OSStatus r = ExtAudioFileRead(p->ext, &got, io);
  if (r) got = 0;
  for (UInt32 b = 0; b < io->mNumberBuffers; b++) { char *m = io->mBuffers[b].mData; UInt32 bpf = p->bpf; memset(m + (size_t)got * bpf, 0, (size_t)(n - got) * bpf); io->mBuffers[b].mDataByteSize = n * bpf; }
  p->frames += got;
  return 0;
}
static void play(const char *path, AudioStreamBasicDescription cf, double secs, UInt64 expect) {
  AudioComponentDescription d = { kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0 };
  AudioComponent c = AudioComponentFindNext(NULL, &d); OK(c != NULL, "kein Ausgabe-Unit");
  AudioUnit u; CK(AudioComponentInstanceNew(c, &u));
  Play p = { 0 }; p.bpf = cf.mBytesPerFrame; CFURLRef uu = url(path); CK(ExtAudioFileOpenURL(uu, &p.ext)); CFRelease(uu);
  CK(ExtAudioFileSetProperty(p.ext, kExtAudioFileProperty_ClientDataFormat, sizeof cf, &cf));
  CK(AudioUnitSetProperty(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &cf, sizeof cf));
  AURenderCallbackStruct cb = { pcb, &p }; CK(AudioUnitSetProperty(u, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb));
  OK(AudioOutputUnitStart(u) == kAudioUnitErr_Uninitialized, "Start vor Initialize");
  CK(AudioUnitInitialize(u)); CK(AudioOutputUnitStart(u));
  UInt32 run = 0, sz = 4; CK(AudioUnitGetProperty(u, kAudioOutputUnitProperty_IsRunning, kAudioUnitScope_Global, 0, &run, &sz)); OK(run == 1, "IsRunning");
  usleep((useconds_t)(secs * 1e6));
  CK(AudioOutputUnitStop(u)); CK(AudioUnitUninitialize(u)); CK(AudioComponentInstanceDispose(u));
  printf("Ausgabe-Unit (%g Hz, %u Kanäle, %u Bit): Callback lieferte %llu Frames (Datei %llu)\n", cf.mSampleRate, (unsigned)cf.mChannelsPerFrame, (unsigned)cf.mBitsPerChannel, p.frames, expect);
  OK(p.frames > 0, "Callback nicht aufgerufen");
  ExtAudioFileDispose(p.ext);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  const char *mode = argc > 1 ? argv[1] : "self"; double secs = argc > 2 ? atof(argv[2]) : 2;
  if (!strcmp(mode, "self")) {
    test_files(); test_components();
    CK(write_file("/tmp/at-play.wav", kAudioFileWAVEType, pcm(48000, 2, 16, 0, 0, 0), secs + 0.5));
    play("/tmp/at-play.wav", pcm(48000, 2, 32, 1, 0, 0), 0.5, 0);
    play("/tmp/at-play.wav", pcm(44100, 1, 16, 0, 0, 0), 0.5, 0);
  } else if (!strcmp(mode, "play")) {
    CK(write_file("/tmp/at-play.wav", kAudioFileWAVEType, pcm(48000, 2, 16, 0, 0, 0), secs + 0.5));
    play("/tmp/at-play.wav", pcm(48000, 2, 32, 1, 0, 0), secs, 0);
  }
  printf(fails ? "%d FEHLER\n" : "alle Prüfungen ok\n", fails);
  return fails != 0;
}

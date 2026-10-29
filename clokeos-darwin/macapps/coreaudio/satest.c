// Prüfprogramm: 2 s Sinus mit 440 Hz, nur über die öffentliche Core-Audio-HAL.
#include <CoreAudio/CoreAudio.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>

static double phase;
static OSStatus io(AudioObjectID d, const AudioTimeStamp *now, const AudioBufferList *in, const AudioTimeStamp *it,
                   AudioBufferList *out, const AudioTimeStamp *ot, void *c) {
  float *p = out->mBuffers[0].mData; UInt32 n = out->mBuffers[0].mDataByteSize / (2 * sizeof(float));
  for (UInt32 i = 0; i < n; i++) { p[2 * i] = p[2 * i + 1] = 0.5f * sinf(phase); phase += 2 * M_PI * 440.0 / 48000.0; if (phase > 2 * M_PI) phase -= 2 * M_PI; }
  return 0;
}
#define CHK(x) do { OSStatus s_ = (x); if (s_) { fprintf(stderr, "%s: %d\n", #x, (int)s_); return 1; } } while (0)
int main(void) {
  AudioObjectPropertyAddress a = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
  AudioObjectID dev; UInt32 sz = sizeof dev;
  CHK(AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, &dev));
  a.mSelector = kAudioDevicePropertyNominalSampleRate; Float64 sr; sz = sizeof sr;
  CHK(AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &sr));
  a.mSelector = kAudioDevicePropertyBufferFrameSize; UInt32 fs; sz = sizeof fs;
  CHK(AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &fs));
  a.mSelector = kAudioStreamPropertyVirtualFormat; AudioStreamBasicDescription f; sz = sizeof f;
  AudioObjectPropertyAddress sa = { kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain };
  AudioObjectID st; sz = sizeof st;
  CHK(AudioObjectGetPropertyData(dev, &sa, 0, NULL, &sz, &st));
  sz = sizeof f; CHK(AudioObjectGetPropertyData(st, &a, 0, NULL, &sz, &f));
  printf("Gerät %u, %.0f Hz, %u Frames, %u Kanäle, %u Bit\n", (unsigned)dev, sr, (unsigned)fs, (unsigned)f.mChannelsPerFrame, (unsigned)f.mBitsPerChannel);
  AudioDeviceIOProcID id;
  CHK(AudioDeviceCreateIOProcID(dev, io, NULL, &id));
  CHK(AudioDeviceStart(dev, id));
  sleep(2);
  CHK(AudioDeviceStop(dev, id));
  CHK(AudioDeviceDestroyIOProcID(dev, id));
  puts("fertig");
  return 0;
}

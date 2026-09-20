"""Run the production mixer and AAudio pacing functions against deterministic devices."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT / "third_party/ut99dc/Source/Audio/Src"
SDL_AUDIO = ROOT / "third_party/SDL2/src/audio/aaudio"


def function(source, name):
    lexed = re.sub(
        r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda match: re.sub(r"[^\n]", " ", match.group()),
        source,
        flags=re.S,
    )
    match = re.search(
        r"^(?:static\s+)?(?:void\s*\*?|int|INT|UBOOL)\s+" + re.escape(name) + r"\s*\(",
        lexed,
        re.M,
    )
    if match is None:
        raise ValueError(f"Production function not found: {name}")
    opening = lexed.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (lexed[end] == "{") - (lexed[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


MIXER_STUBS = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <strings.h>
#define __ANDROID__ 1
#define TEXT(x) x
#define ALock AudioLock(&Mutex)
#define AUnlock AudioUnlock(&Mutex)
#define CheckAudioLib(f) if(!AudioInitialized) return f;
using INT=int; using UBOOL=int; using DWORD=unsigned; using BYTE=unsigned char;
using SWORD=short; using DOUBLE=double; using Uint32=uint32_t;
using SDL_AudioDeviceID=unsigned;
enum { AUDIO_STEREO=1, AUDIO_16BIT=2, SAMPLE_8BIT=1, SAMPLE_16BIT=2,
       VOICE_ENABLED=1, VOICE_ACTIVE=2, AUDIO_TOTALVOICES=2,
       NAME_Init=1, NAME_Warning=2, SDL_INIT_AUDIO=1, AUDIO_S16SYS=0x8010,
       SDL_AUDIO_ALLOW_FREQUENCY_CHANGE=1, SDL_THREAD_PRIORITY_HIGH=2,
       SDL_AUDIO_STOPPED=0, SDL_AUDIO_PLAYING=1, SDL_AUDIO_PAUSED=2 };
struct SDL_AudioSpec { int freq; int format; int channels; int samples; void* callback; void* userdata; };
struct AudioThread { int Valid=0; int Exited=0; };
struct Sample { int Type=SAMPLE_16BIT; };
struct Voice { int State=0; Sample* pSample=nullptr; int PlayPosition=0; };
int Mutex, LockDepth=0, AudioDevice=-1, BufferSize=0, AudioFormat=0, AudioRate=0;
int AudioInitialized=0, AudioPaused=0, Warnings=0, Mixes=0, Sleeps=0, Clears=0;
int LastClearLockDepth=-1, Joined=0, DestroyedMutex=0;
void* AudioBuffer=nullptr;
void* MixBuffer=nullptr;
AudioThread MixingThread;
Voice Voices[AUDIO_TOTALVOICES];
static SDL_AudioDeviceID AndroidAudioDevice=0;
static DOUBLE LastAndroidAudioQueueTime=0, LastAndroidAudioLateLog=0;
static unsigned Queued=0, PeakQueued=0;
static int DeviceStatus=SDL_AUDIO_PLAYING, BadSpec=0, WorldSound=1, QueueError=0;
static double Clock=2;
static const char* Driver="AAudio";
static std::function<void()> OnSleep;
static std::function<void()> OnStatus;
static bool StopAfterQueue=false;
static int LastQueuedSound=-1;
template<typename T> T Max(T a,T b) { return std::max(a,b); }
template<typename T> T Clamp(T v,T a,T b) { return std::min(std::max(v,a),b); }
static int AudioLock(int*) { ++LockDepth; return 1; }
static int AudioUnlock(int*) { assert(LockDepth>0); --LockDepth; return 1; }
static void* appMalloc(int size,const char*) { return malloc(size); }
static void appFree(void* p) { free(p); }
static void appMemset(void* p,int value,int size) { memset(p,value,size); }
static void appMemcpy(void* to,const void* from,int size) { memcpy(to,from,size); }
static double appSeconds() { return Clock; }
static const char* appFromAnsi(const char* s) { return s; }
static void debugf(int level,const char*,...) { if(level==NAME_Warning) ++Warnings; }
static int SDL_strcasecmp(const char* a,const char* b) { return strcasecmp(a,b); }
static unsigned SDL_WasInit(unsigned flag) { return flag; }
static int SDL_InitSubSystem(unsigned) { return 0; }
static const char* SDL_GetCurrentAudioDriver() { return Driver; }
static const char* SDL_GetError() { return "test error"; }
static SDL_AudioDeviceID SDL_OpenAudioDevice(const char*,int,const SDL_AudioSpec* want,SDL_AudioSpec* have,int) {
    *have=*want;
    have->freq=!strcasecmp(Driver,"AAudio") ? 48000 : want->freq;
    if(BadSpec) have->samples=0;
    DeviceStatus=SDL_AUDIO_PAUSED;
    return 1;
}
static int SDL_GetAudioDeviceStatus(unsigned) {
    assert(LockDepth>=0 && LockDepth<=2);
    if(OnStatus) { auto action=OnStatus; OnStatus=nullptr; action(); }
    return DeviceStatus;
}
static void SDL_PauseAudioDevice(unsigned,int paused) { DeviceStatus=paused ? SDL_AUDIO_PAUSED : SDL_AUDIO_PLAYING; }
static void SDL_CloseAudioDevice(unsigned) { DeviceStatus=SDL_AUDIO_STOPPED; }
static void SDL_ClearQueuedAudio(unsigned) { Queued=0; ++Clears; LastClearLockDepth=LockDepth; }
static unsigned SDL_GetQueuedAudioSize(unsigned) { assert(LockDepth==0); return Queued; }
static int SDL_QueueAudio(unsigned,const void* data,unsigned length) {
    assert(LockDepth==1);
    assert(Queued==0);
    if(QueueError) return -1;
    Queued+=length;
    PeakQueued=std::max(PeakQueued,Queued);
    LastQueuedSound=static_cast<const BYTE*>(data)[0];
    if(StopAfterQueue) MixingThread.Valid=0;
    return 0;
}
static void AudioSleep(int ms) {
    assert(LockDepth==0);
    assert(++Sleeps<100);
    Clock+=ms/1000.0;
    if(OnSleep) { auto action=OnSleep; OnSleep=nullptr; action(); }
}
static int SDL_SetThreadPriority(int) { return 0; }
static void MixVoice8to16(int) { assert(LockDepth==1); ++Mixes; memset(MixBuffer,WorldSound,BufferSize); }
static void MixVoice16to16(int i) { MixVoice8to16(i); }
static void MixMusicIntoBuffer() {}
static int ExitAudioThread(AudioThread* thread) { thread->Exited=1; return 1; }
static int DestroyAudioThread(AudioThread* thread) {
    assert(LockDepth==0 && AudioInitialized==0);
    thread->Valid=0; ++Joined; return 1;
}
static int DestroyAudioMutex(int*) { assert(LockDepth==0); ++DestroyedMutex; return 1; }
"""

MIXER_TESTS = r"""
int main() {
    assert(OpenAudio(22050,AUDIO_STEREO,40));
    assert(AudioRate==48000 && BufferSize==512*2*2);
    CloseAudio();
    Driver="openslES";
    assert(OpenAudio(22050,AUDIO_STEREO,40));
    assert(AudioRate==22050 && BufferSize==1024*2*2);
    CloseAudio();
    assert(OpenAudio(22050,AUDIO_STEREO,20));
    assert(BufferSize==512*2*2);
    CloseAudio();
    BadSpec=1;
    assert(!OpenAudio(22050,AUDIO_STEREO,20));
    assert(!AndroidAudioDevice && AudioDevice==-1 && !AudioBuffer);
    BadSpec=0;
    Driver="AAudio";
    assert(OpenAudio(22050,AUDIO_STEREO,40));
    MixingThread.Valid=1; AudioInitialized=1; AudioPaused=1;
    int originalSize=BufferSize;
    assert(AudioStartOutput(44100,AUDIO_STEREO,10));
    assert(BufferSize==originalSize && AudioRate==48000 && !AudioPaused && LockDepth==0);
    DeviceStatus=SDL_AUDIO_STOPPED;
    assert(!AudioStartOutput(44100,AUDIO_STEREO,10));
    assert(AudioPaused && BufferSize==originalSize && LockDepth==0);
    DeviceStatus=SDL_AUDIO_PLAYING; AudioPaused=0;
    int previousWarnings=Warnings;
    QueueError=1;
    ALock; PlayAudio(); AUnlock;
    assert(Warnings==previousWarnings+1 && Queued==0 && LastAndroidAudioQueueTime==0);
    QueueError=0;
    Sample sample;
    Voices[0].pSample=&sample;
    Queued=BufferSize;
    OnSleep=[&] {
        assert(Mixes==0);
        ALock;
        WorldSound=42;
        Voices[0].State=VOICE_ENABLED|VOICE_ACTIVE;
        AUnlock;
        Queued=0;
    };
    StopAfterQueue=true;
    DoSound(nullptr);
    assert(Mixes==1 && LastQueuedSound==42);
    assert(PeakQueued==unsigned(BufferSize) && !MixBuffer && MixingThread.Exited);

    MixingThread.Valid=1; AudioPaused=0;
    Sleeps=0; Queued=BufferSize;
    OnSleep=[] { ALock; MixingThread.Valid=0; AUnlock; };
    assert(!AudioWait() && Sleeps==1);
    MixingThread.Valid=1; AudioPaused=0;
    Sleeps=0;
    OnSleep=[] { ALock; AudioPaused=1; AUnlock; };
    assert(!AudioWait() && Sleeps==1);
    AudioPaused=0; DeviceStatus=SDL_AUDIO_PAUSED;
    Sleeps=0; Queued=BufferSize;
    int previousClears=Clears;
    OnSleep=[] { DeviceStatus=SDL_AUDIO_PLAYING; };
    assert(AudioWait());
    assert(Sleeps==1 && Queued==0 && Clears==previousClears+1);

    MixingThread.Valid=1; AudioPaused=0; Sleeps=0;
    Queued=0;
    OnStatus=[] { ALock; AudioPaused=1; AUnlock; };
    OnSleep=[] { ALock; MixingThread.Valid=0; AUnlock; };
    int previousMixes=Mixes;
    DoSound(nullptr);
    assert(Mixes==previousMixes && !MixBuffer);

    Queued=BufferSize; AudioPaused=0;
    Voices[0].State=VOICE_ENABLED|VOICE_ACTIVE;
    AudioStopOutput();
    assert(AudioPaused && Queued==0 && LastClearLockDepth==0);
    assert(!(Voices[0].State&VOICE_ACTIVE));
    assert(LastAndroidAudioQueueTime==0);
    AudioInitialized=1;
    AudioShutdown();
    assert(Joined==1 && DestroyedMutex==1 && !AndroidAudioDevice && !AudioBuffer);
    assert(LockDepth==0);
    puts("Audio mixer: fresh-state mixing, bounded queue, pause and shutdown checks passed.");
}
"""

AAUDIO_STUBS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define _THIS SDL_AudioDevice *this
#define SDL_min(a,b) ((a)<(b)?(a):(b))
#define SDL_max(a,b) ((a)>(b)?(a):(b))
#define SDL_LOG_CATEGORY_AUDIO 0
#define SDL_FALSE 0
#define SDL_TRUE 1
typedef int SDL_bool;
typedef uint8_t Uint8;
typedef uint32_t Uint32;
typedef int32_t aaudio_result_t;
typedef int32_t aaudio_stream_state_t;
typedef struct { int unused; } AAudioStream;
typedef struct { int value; } SDL_atomic_t;
enum { AAUDIO_ERROR_TIMEOUT=-885, AAUDIO_STREAM_STATE_STARTED=4,
       AAUDIO_STREAM_STATE_PAUSING=5, AAUDIO_STREAM_STATE_PAUSED=6 };
"""

AAUDIO_DEVICE = r"""
typedef struct {
    struct SDL_PrivateAudioData *hidden;
    struct { int freq; } spec;
    SDL_atomic_t shutdown,paused,enabled;
} SDL_AudioDevice;
static Uint32 Clock=2000, Delayed=0;
static int Warnings=0, Errors=0, Disconnections=0, Writes=0, SetCalls=0;
static int Burst=16, Capacity=128, BufferFrames=128, Xruns=0, State=AAUDIO_STREAM_STATE_STARTED;
static int Results[16], ResultCount=0, ResultIndex=0;
static int Accepted=0, PauseOnWrite=0, BytesPerFrame=4;
static Uint8 AcceptedData[256];
static SDL_AudioDevice *Device;
static int SDL_AtomicGet(SDL_atomic_t* value) { return value->value; }
static Uint32 SDL_GetTicks(void) { return Clock; }
static void SDL_Delay(Uint32 ms) { Delayed+=ms; Clock+=ms; }
static void SDL_LogWarn(int category,const char* text,...) { (void)category; (void)text; ++Warnings; }
static void SDL_LogError(int category,const char* text,...) { (void)category; (void)text; ++Errors; }
static void SDL_LogInfo(int category,const char* text,...) { (void)category; (void)text; }
static void SDL_OpenedAudioDeviceDisconnected(SDL_AudioDevice *device) { device->enabled.value=0; ++Disconnections; }
static const char* error_text(aaudio_result_t value) { (void)value; return "test error"; }
static int32_t get_capacity(AAudioStream* s) { (void)s; return Capacity; }
static int32_t get_burst(AAudioStream* s) { (void)s; return Burst; }
static int32_t get_size(AAudioStream* s) { (void)s; return BufferFrames; }
static int32_t set_size(AAudioStream* s,int32_t frames) { (void)s; ++SetCalls; BufferFrames=frames; return frames; }
static int32_t get_performance(AAudioStream* s) { (void)s; return 12; }
static int32_t get_xruns(AAudioStream* s) { (void)s; return Xruns; }
static int32_t get_state(AAudioStream* s) { (void)s; return State; }
static aaudio_result_t write_frames(AAudioStream* stream,const void* data,int32_t count,int64_t timeout) {
    int result;
    (void)stream;
    assert(timeout>1000000);
    assert(ResultIndex<ResultCount);
    result=Results[ResultIndex++];
    ++Writes;
    if(result>0) {
        assert(result<=count);
        memcpy(AcceptedData+Accepted*BytesPerFrame,data,result*BytesPerFrame);
        Accepted+=result;
    }
    if(PauseOnWrite) Device->paused.value=1;
    return result;
}
static struct {
    int32_t (*AAudioStream_getBufferCapacityInFrames)(AAudioStream*);
    int32_t (*AAudioStream_getFramesPerBurst)(AAudioStream*);
    int32_t (*AAudioStream_getBufferSizeInFrames)(AAudioStream*);
    int32_t (*AAudioStream_setBufferSizeInFrames)(AAudioStream*,int32_t);
    int32_t (*AAudioStream_getPerformanceMode)(AAudioStream*);
    int32_t (*AAudioStream_getXRunCount)(AAudioStream*);
    int32_t (*AAudioStream_getState)(AAudioStream*);
    const char* (*AAudio_convertResultToText)(aaudio_result_t);
    aaudio_result_t (*AAudioStream_write)(AAudioStream*,const void*,int32_t,int64_t);
} ctx={get_capacity,get_burst,get_size,set_size,get_performance,get_xruns,get_state,error_text,write_frames};
static void responses(int a,int b,int c) {
    Results[0]=a; Results[1]=b; Results[2]=c;
    ResultCount=3; ResultIndex=0; Writes=0; Accepted=0;
}
"""

AAUDIO_TESTS = r"""
int main(void) {
    Uint8 data[64];
    struct SDL_PrivateAudioData private={0};
    SDL_AudioDevice device={0};
    int i;
    for(i=0;i<64;++i) data[i]=(Uint8)i;
    device.hidden=&private;
    device.spec.freq=48000;
    device.enabled.value=1;
    Device=&device;
    private.mixbuf=data;
    aaudio_ConfigureOutputBuffer(&device);
    assert(BufferFrames==32 && private.buffer_limit_frames==64);
    for(BytesPerFrame=2;BytesPerFrame<=8;BytesPerFrame*=2) {
        private.frame_size=BytesPerFrame;
        private.mixlen=8*BytesPerFrame;
        responses(2,1,5);
        aaudio_PlayDevice(&device);
        assert(Writes==3 && Accepted==8);
        assert(!memcmp(data,AcceptedData,private.mixlen));
    }
    BytesPerFrame=private.frame_size=4; private.mixlen=32;
    responses(0,AAUDIO_ERROR_TIMEOUT,8);
    aaudio_PlayDevice(&device);
    assert(Accepted==8 && Warnings==0);
    responses(0,0,0);
    aaudio_PlayDevice(&device);
    assert(Writes==3 && Accepted==0 && Warnings==1 && Delayed>0);
    responses(0,0,0);
    aaudio_PlayDevice(&device);
    assert(Warnings==1);
    device.paused.value=1;
    responses(8,0,0);
    Delayed=0;
    aaudio_PlayDevice(&device);
    assert(Writes==0 && Delayed>0);
    device.paused.value=0;
    PauseOnWrite=1;
    responses(2,6,0);
    aaudio_PlayDevice(&device);
    assert(Writes==1 && Accepted==2 && Disconnections==0);
    device.paused.value=0; PauseOnWrite=0;
    State=AAUDIO_STREAM_STATE_PAUSING;
    responses(-999,0,0);
    aaudio_PlayDevice(&device);
    assert(Disconnections==0 && Errors==0);
    State=AAUDIO_STREAM_STATE_STARTED;
    responses(-999,0,0);
    aaudio_PlayDevice(&device);
    assert(Disconnections==1 && Errors==1 && !device.enabled.value);
    device.enabled.value=1;
    device.shutdown.value=1;
    responses(8,0,0);
    aaudio_PlayDevice(&device);
    assert(Writes==0);
    device.shutdown.value=0;
    Xruns=1;
    responses(8,0,0);
    aaudio_PlayDevice(&device);
    assert(BufferFrames==48);
    Xruns=2;
    responses(8,0,0);
    aaudio_PlayDevice(&device);
    assert(BufferFrames==64);
    Xruns=3;
    responses(8,0,0);
    aaudio_PlayDevice(&device);
    assert(BufferFrames==64);
    Capacity=24; BufferFrames=24;
    aaudio_ConfigureOutputBuffer(&device);
    assert(BufferFrames==24 && private.buffer_limit_frames==24);
    puts("AAudio: complete partial writes, bounded stalls, pause and adaptive-buffer checks passed.");
    return 0;
}
"""


def main():
    core = (AUDIO / "AudioCoreLinux.cpp").read_text()
    library = (AUDIO / "AudioLibrary.cpp").read_text()
    mixer = (AUDIO / "AudioMixer.cpp").read_text()
    aaudio = (SDL_AUDIO / "SDL_aaudio.c").read_text()
    aaudio_header = (SDL_AUDIO / "SDL_aaudio.h").read_text()
    private = re.search(r"struct SDL_PrivateAudioData\s*\{.*?\n\};", aaudio_header, re.S).group()

    mixer_code = MIXER_STUBS + "".join(function(core, name) for name in (
        "CloseAudio", "NextPowerOfTwo", "OpenAudio", "ReopenAudioDevice",
        "ClearAudioQueue", "PlayAudio", "AudioWait",
    ))
    mixer_code += function(mixer, "DoSound")
    mixer_code += function(library, "AudioReinit") + function(library, "AudioStartOutput")
    mixer_code += function(library, "AudioStopOutput") + function(library, "AudioShutdown") + MIXER_TESTS
    aaudio_code = AAUDIO_STUBS + private + AAUDIO_DEVICE + "".join(function(aaudio, name) for name in (
        "aaudio_ConfigureOutputBuffer", "aaudio_CheckUnderruns", "aaudio_PlayDevice",
    )) + AAUDIO_TESTS

    open_device = function(aaudio, "aaudio_OpenDevice")
    assert "iscapture ? this->spec.freq : AAUDIO_UNSPECIFIED" in open_device
    assert "iscapture ? AAUDIO_PERFORMANCE_MODE_NONE : AAUDIO_PERFORMANCE_MODE_LOW_LATENCY" in open_device
    sanitizer = ["-fsanitize=address,undefined"] if "--sanitize" in sys.argv[1:] else []
    with tempfile.TemporaryDirectory(prefix="ut99-audio-test-") as temp:
        for name, compiler, standard, code in (
            ("mixer.cpp", "c++", "-std=c++17", mixer_code),
            ("aaudio.c", "cc", "-std=c11", aaudio_code),
        ):
            path = Path(temp) / name
            binary = path.with_suffix("")
            path.write_text(code)
            subprocess.run([compiler, standard, "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                            "-Wno-unused-function", *sanitizer, str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()

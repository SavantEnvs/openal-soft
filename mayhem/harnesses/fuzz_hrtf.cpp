// fuzz_hrtf: the .mhr HRTF dataset loader (core/hrtf_loader.cpp, all format versions) via
// the documented config path: alsoft.conf `hrtf-paths` names a directory, `default-hrtf`
// selects the dataset by file stem. Each input is written to <dir>/fuzz.mhr, then a fresh
// loopback device is opened (HRTFs are enumerated per device open) and a context is
// created with ALC_HRTF_SOFT, so the dataset is parsed, resampled to the device rate,
// built into the HRTF store and mixed. The device is closed after every input, which drops
// the loaded-HRTF cache, so each input is parsed from scratch.
#include "common.h"

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    fz::setup_env("[general]\nhrtf-paths = @DIR@\ndefault-hrtf = fuzz\nhrtf = true\n");
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if(!fz::write_file(fz::g_dir + "/fuzz.mhr", data, size)) abort();
    ALCdevice *dev = alcLoopbackOpenDeviceSOFT(nullptr);
    if(!dev) abort();
    // Rate follows the input size parity so both the native-rate and the resampling
    // paths of the loader are reached.
    const ALCint rate = (size & 1) ? 48000 : 44100;
    const ALCint attrs[] = {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
        ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT, ALC_FREQUENCY, rate,
        ALC_HRTF_SOFT, ALC_TRUE, 0};
    ALCcontext *ctx = alcCreateContext(dev, attrs);
    if(ctx && alcMakeContextCurrent(ctx))
    {
        static const int16_t tone[256] = {0, 8000, 16000, 8000, 0, -8000, -16000, -8000};
        ALuint buf = 0, src = 0;
        alGenBuffers(1, &buf);
        alBufferData(buf, AL_FORMAT_MONO16, tone, sizeof(tone), 44100);
        alGenSources(1, &src);
        alSourcei(src, AL_BUFFER, ALint(buf));
        alSourcei(src, AL_LOOPING, AL_TRUE);
        alSource3f(src, AL_POSITION, 1.0f, 0.3f, -0.5f);
        alSourcePlay(src);
        fz::render(dev, 2);
        alDeleteSources(1, &src);
        alDeleteBuffers(1, &buf);
        alcMakeContextCurrent(nullptr);
    }
    if(ctx) alcDestroyContext(ctx);
    alcCloseDevice(dev);
    return 0;
}

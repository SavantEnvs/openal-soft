// fuzz_ambdec: the AmbDec decoder-layout parser (core/ambdec.cpp AmbDecConf::load) and the
// custom-decoder panning setup it feeds (alc/panning.cpp), via the documented config path:
// alsoft.conf `[decoder] <layout> = <file>`. Byte 0 picks the output layout (which config
// key is consulted); the rest of the input is written as the .ambdec file. A loopback device
// + context is created per input with that channel layout and one block is rendered.
#include "common.h"

static const ALCint kLayouts[] = {ALC_QUAD_SOFT, ALC_5POINT1_SOFT, ALC_6POINT1_SOFT,
    ALC_7POINT1_SOFT};

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    fz::setup_env("[decoder]\nhq-mode = true\ndistance-comp = true\nnfc = true\n"
        "quad = @DIR@/fuzz.ambdec\nsurround51 = @DIR@/fuzz.ambdec\n"
        "surround61 = @DIR@/fuzz.ambdec\nsurround71 = @DIR@/fuzz.ambdec\n");
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if(size < 1) return -1;
    const ALCint chans = kLayouts[data[0] & 3];
    const bool hq = data[0] & 4;
    if(!fz::write_file(fz::g_dir + "/fuzz.ambdec", data + 1, size - 1)) abort();
    ALCdevice *dev = alcLoopbackOpenDeviceSOFT(nullptr);
    if(!dev) abort();
    const ALCint attrs[] = {ALC_FORMAT_CHANNELS_SOFT, chans, ALC_FORMAT_TYPE_SOFT,
        ALC_FLOAT_SOFT, ALC_FREQUENCY, hq ? 48000 : 44100, 0};
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
        alSource3f(src, AL_POSITION, -1.0f, 0.2f, 0.5f);
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

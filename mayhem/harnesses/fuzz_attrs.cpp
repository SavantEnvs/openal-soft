// fuzz_attrs: ALC attribute lists (alc/alc.cpp UpdateDeviceParams: format, frequency,
// ambisonic layout/order, HRTF, output mode/limiter, source/send counts) on a loopback
// device. The input is read as little-endian int32 key/value pairs: keys are taken from a
// table of the attributes the library accepts (byte-indexed), values are raw int32 (so
// negative/huge frequencies, orders and counts are reached). The first list creates the
// context; an optional second list (after a zero key) goes to alcResetDeviceSOFT. The
// device is opened and closed per input.
#include "common.h"

static const ALCint kKeys[] = {ALC_FREQUENCY, ALC_REFRESH, ALC_SYNC, ALC_MONO_SOURCES,
    ALC_STEREO_SOURCES, ALC_MAX_AUXILIARY_SENDS, ALC_FORMAT_CHANNELS_SOFT,
    ALC_FORMAT_TYPE_SOFT, ALC_HRTF_SOFT, ALC_HRTF_ID_SOFT, ALC_AMBISONIC_LAYOUT_SOFT,
    ALC_AMBISONIC_SCALING_SOFT, ALC_AMBISONIC_ORDER_SOFT, ALC_OUTPUT_LIMITER_SOFT,
    ALC_OUTPUT_MODE_SOFT, ALC_CONTEXT_FLAGS_EXT};
static const ALCint kVals[] = {ALC_MONO_SOFT, ALC_STEREO_SOFT, ALC_QUAD_SOFT,
    ALC_5POINT1_SOFT, ALC_6POINT1_SOFT, ALC_7POINT1_SOFT, ALC_BFORMAT3D_SOFT,
    ALC_BYTE_SOFT, ALC_UNSIGNED_BYTE_SOFT, ALC_SHORT_SOFT, ALC_UNSIGNED_SHORT_SOFT,
    ALC_INT_SOFT, ALC_UNSIGNED_INT_SOFT, ALC_FLOAT_SOFT, ALC_FUMA_SOFT, ALC_ACN_SOFT,
    ALC_SN3D_SOFT, ALC_N3D_SOFT, ALC_STEREO_BASIC_SOFT, ALC_STEREO_UHJ_SOFT,
    ALC_STEREO_HRTF_SOFT, ALC_ANY_SOFT, ALC_TRUE, ALC_FALSE, 44100, 48000, 22050, 8000};

// One pair: [keyidx:1][mode:1][value:4]. mode&1 → value from kVals table, else raw int32.
static size_t read_list(fz::Reader &r, std::vector<ALCint> &out)
{
    while(r.n >= 6 && out.size() < 64)
    {
        const uint8_t k = r.u8();
        if(k == 0xff) break;
        const uint8_t mode = r.u8();
        const uint32_t v = r.u32();
        out.push_back(kKeys[k % (sizeof(kKeys)/sizeof(kKeys[0]))]);
        out.push_back((mode & 1) ? kVals[v % (sizeof(kVals)/sizeof(kVals[0]))] : ALCint(v));
    }
    out.push_back(0);
    return out.size();
}

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    fz::setup_env("[general]\n");
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fz::Reader r{data, size};
    std::vector<ALCint> a1, a2;
    read_list(r, a1);
    const bool reset = r.n > 0;
    if(reset) read_list(r, a2);

    ALCdevice *dev = alcLoopbackOpenDeviceSOFT(nullptr);
    if(!dev) abort();
    ALCint chans = 0, type = 0, freq = 0;
    for(size_t i = 0; i + 1 < a1.size(); i += 2)
    {
        if(a1[i] == ALC_FORMAT_CHANNELS_SOFT) chans = a1[i+1];
        if(a1[i] == ALC_FORMAT_TYPE_SOFT) type = a1[i+1];
        if(a1[i] == ALC_FREQUENCY) freq = a1[i+1];
    }
    alcIsRenderFormatSupportedSOFT(dev, freq, chans, type);
    ALCcontext *ctx = alcCreateContext(dev, a1.data());
    if(ctx && alcMakeContextCurrent(ctx))
    {
        ALCint n = 0;
        alcGetIntegerv(dev, ALC_ATTRIBUTES_SIZE, 1, &n);
        if(n > 0 && n < 256)
        {
            std::vector<ALCint> got(static_cast<size_t>(n));
            alcGetIntegerv(dev, ALC_ALL_ATTRIBUTES, n, got.data());
        }
        static const int16_t tone[64] = {0, 8000, 16000, 8000, 0, -8000, -16000, -8000};
        ALuint buf = 0, src = 0;
        alGenBuffers(1, &buf);
        alBufferData(buf, AL_FORMAT_MONO16, tone, sizeof(tone), 22050);
        alGenSources(1, &src);
        alSourcei(src, AL_BUFFER, ALint(buf));
        alSourcei(src, AL_LOOPING, AL_TRUE);
        alSource3f(src, AL_POSITION, 0.5f, 0.0f, -1.0f);
        alSourcePlay(src);
        fz::render(dev, 1, 256);
        if(reset && alcResetDeviceSOFT(dev, a2.data()))
            fz::render(dev, 1, 256);
        alDeleteSources(1, &src);
        alDeleteBuffers(1, &buf);
        alcMakeContextCurrent(nullptr);
    }
    if(ctx) alcDestroyContext(ctx);
    alcCloseDevice(dev);
    return 0;
}

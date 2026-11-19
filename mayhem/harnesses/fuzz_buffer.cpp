// fuzz_buffer: application sample data through the public AL buffer API.
// Input layout: [fmt:1][align:2][freq:4][offset:4][pitch:1][flags:1][split:2] then sample data.
// The data is loaded with alBufferData (after AL_UNPACK_BLOCK_ALIGNMENT_SOFT), attached to
// a source (or queued as two buffers), and rendered through a loopback device, which runs
// the per-format sample loaders (PCM, mu-law, a-law, IMA4, MS-ADPCM, B-Format, UHJ), the
// resamplers and the mixers. A loopback device + context are opened once (read-only setup);
// every buffer/source is deleted after each input.
#include "common.h"

static const ALenum kFormats[] = {
    AL_FORMAT_MONO8, AL_FORMAT_MONO16, AL_FORMAT_STEREO8, AL_FORMAT_STEREO16,
    AL_FORMAT_MONO_FLOAT32, AL_FORMAT_STEREO_FLOAT32, AL_FORMAT_MONO_DOUBLE_EXT,
    AL_FORMAT_STEREO_DOUBLE_EXT, AL_FORMAT_MONO_MULAW_EXT, AL_FORMAT_STEREO_MULAW_EXT,
    AL_FORMAT_MONO_ALAW_EXT, AL_FORMAT_STEREO_ALAW_EXT, AL_FORMAT_MONO_IMA4,
    AL_FORMAT_STEREO_IMA4, AL_FORMAT_MONO_MSADPCM_SOFT, AL_FORMAT_STEREO_MSADPCM_SOFT,
    AL_FORMAT_QUAD16, AL_FORMAT_51CHN16, AL_FORMAT_61CHN16, AL_FORMAT_71CHN16,
    AL_FORMAT_REAR16, AL_FORMAT_QUAD_MULAW, AL_FORMAT_51CHN_MULAW,
    AL_FORMAT_BFORMAT2D_16, AL_FORMAT_BFORMAT3D_16, AL_FORMAT_BFORMAT2D_FLOAT32,
    AL_FORMAT_BFORMAT3D_FLOAT32, AL_FORMAT_BFORMAT2D_MULAW, AL_FORMAT_BFORMAT3D_MULAW,
    AL_FORMAT_UHJ2CHN16_SOFT, AL_FORMAT_UHJ3CHN16_SOFT, AL_FORMAT_UHJ4CHN16_SOFT,
    AL_FORMAT_UHJ2CHN_IMA4_SOFT, AL_FORMAT_UHJ2CHN_MSADPCM_SOFT, AL_FORMAT_UHJ2CHN_MULAW_SOFT,
    AL_FORMAT_UHJ2CHN_FLOAT32_SOFT, AL_FORMAT_MONO_ALAW_EXT, AL_FORMAT_STEREO16,
    AL_FORMAT_MONO_FLOAT32, AL_FORMAT_STEREO8,
};

static ALCdevice *g_dev;
static ALCcontext *g_ctx;

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    fz::setup_env("[general]\nresampler = bsinc24\n");
    g_dev = alcLoopbackOpenDeviceSOFT(nullptr);
    if(!g_dev) abort();
    const ALCint attrs[] = {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
        ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT, ALC_FREQUENCY, 44100, 0};
    g_ctx = alcCreateContext(g_dev, attrs);
    if(!g_ctx || !alcMakeContextCurrent(g_ctx)) abort();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if(size < 15) return -1;
    fz::Reader r{data, size};
    const ALenum fmt = kFormats[r.u8() % (sizeof(kFormats)/sizeof(kFormats[0]))];
    const ALint align = r.u16();
    const ALsizei freq = ALsizei(r.u32());
    const ALint offset = ALint(r.u32() & 0xffff);
    const uint8_t pitchb = r.u8();
    const uint8_t flags = r.u8();
    const uint16_t split = r.u16();
    const uint8_t *pcm = r.p;
    const size_t pcmlen = r.n;

    alGetError();
    ALuint bufs[2] = {0, 0};
    alGenBuffers(2, bufs);
    int nbufs = 0;
    const bool queue = flags & 1;
    size_t first = queue ? std::min<size_t>(split, pcmlen) : pcmlen;

    alBufferi(bufs[0], AL_UNPACK_BLOCK_ALIGNMENT_SOFT, align);
    alBufferData(bufs[0], fmt, pcm, ALsizei(first), freq);
    if(alGetError() == AL_NO_ERROR) nbufs = 1;
    if(nbufs && queue)
    {
        alBufferi(bufs[1], AL_UNPACK_BLOCK_ALIGNMENT_SOFT, align);
        alBufferData(bufs[1], fmt, pcm + first, ALsizei(pcmlen - first), freq);
        if(alGetError() == AL_NO_ERROR) nbufs = 2;
    }

    if(nbufs)
    {
        if(flags & 2)
        {
            ALint loop[2] = {ALint(r.u16()), ALint(r.u16())};
            alBufferiv(bufs[0], AL_LOOP_POINTS_SOFT, loop);
            alGetError();
        }
        ALuint src = 0;
        alGenSources(1, &src);
        if(queue) alSourceQueueBuffers(src, nbufs, bufs);
        else alSourcei(src, AL_BUFFER, ALint(bufs[0]));
        alSourcei(src, AL_LOOPING, (flags & 4) ? AL_TRUE : AL_FALSE);
        alSourcef(src, AL_PITCH, 0.0625f + float(pitchb) / 32.0f);
        if(flags & 8) alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
        if(flags & 16) alSource3f(src, AL_POSITION, 1.0f, 0.5f, -1.0f);
        if(flags & 32) alSourcei(src, AL_DIRECT_CHANNELS_SOFT, AL_REMIX_UNMATCHED_SOFT);
        alSourcei(src, AL_SAMPLE_OFFSET, offset);
        alGetError();
        alSourcePlay(src);
        fz::render(g_dev, 4);
        alSourceStop(src);
        alSourcei(src, AL_BUFFER, 0);
        alDeleteSources(1, &src);
    }
    alDeleteBuffers(2, bufs);
    alGetError();
    return 0;
}

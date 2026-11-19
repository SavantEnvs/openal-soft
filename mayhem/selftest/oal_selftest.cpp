// oal_selftest: known-answer functional checks of openal-soft through its public API,
// rendered on a loopback device (no audio hardware). Each check prints "PASS <name>" or
// "FAIL <name>"; mayhem/test.sh counts them. Built with the project's normal flags.
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
static void check(bool ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if(!ok) ++g_fail;
}

static std::vector<float> render(ALCdevice *dev, int frames, int chans = 2)
{
    std::vector<float> out(size_t(frames) * size_t(chans));
    alcRenderSamplesSOFT(dev, out.data(), frames);
    return out;
}

int main()
{
    setenv("ALSOFT_CONF", "/dev/null", 1);
    setenv("ALSOFT_LOGLEVEL", "0", 1);
    ALCdevice *dev = alcLoopbackOpenDeviceSOFT(nullptr);
    check(dev != nullptr, "loopback_open");
    if(!dev) return 1;
    check(alcIsRenderFormatSupportedSOFT(dev, 44100, ALC_STEREO_SOFT, ALC_FLOAT_SOFT) == ALC_TRUE,
        "render_format_supported");
    check(alcIsRenderFormatSupportedSOFT(dev, 44100, 0x1234, ALC_FLOAT_SOFT) == ALC_FALSE,
        "render_format_rejected");
    const ALCint attrs[] = {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT,
        ALC_FLOAT_SOFT, ALC_FREQUENCY, 44100, ALC_HRTF_SOFT, ALC_FALSE, 0};
    ALCcontext *ctx = alcCreateContext(dev, attrs);
    check(ctx && alcMakeContextCurrent(ctx), "context_create");
    if(!ctx) return 1;

    ALCint freq = 0;
    alcGetIntegerv(dev, ALC_FREQUENCY, 1, &freq);
    check(freq == 44100, "device_frequency");
    const char *vendor = alGetString(AL_VENDOR);
    check(vendor && strstr(vendor, "OpenAL Community"), "vendor_string");
    check(alIsExtensionPresent("AL_SOFT_MSADPCM") == AL_TRUE, "ext_msadpcm");

    // Silence with no sources.
    auto quiet = render(dev, 1024);
    float qmax = 0.0f;
    for(float v : quiet) qmax = std::max(qmax, std::fabs(v));
    check(qmax == 0.0f, "silence_without_sources");

    // A centered DC mono source renders equal, non-zero L/R.
    std::vector<int16_t> dc(44100, 16384);
    ALuint buf = 0, src = 0;
    alGenBuffers(1, &buf);
    alBufferData(buf, AL_FORMAT_MONO16, dc.data(), ALsizei(dc.size() * 2), 44100);
    ALint bsize = 0, bfreq = 0, bbits = 0, bchans = 0;
    alGetBufferi(buf, AL_SIZE, &bsize);
    alGetBufferi(buf, AL_FREQUENCY, &bfreq);
    alGetBufferi(buf, AL_BITS, &bbits);
    alGetBufferi(buf, AL_CHANNELS, &bchans);
    check(bsize == 88200 && bfreq == 44100 && bbits == 16 && bchans == 1, "buffer_props");
    alGenSources(1, &src);
    alSourcei(src, AL_BUFFER, ALint(buf));
    alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcePlay(src);
    ALint state = 0;
    alGetSourcei(src, AL_SOURCE_STATE, &state);
    check(state == AL_PLAYING, "source_playing");
    render(dev, 2048);
    auto o = render(dev, 1024);
    const float l = o[1000 * 2], r = o[1000 * 2 + 1];
    check(std::fabs(l - r) < 1e-4f && l > 0.2f && l < 0.6f, "centered_dc_level");
    ALint off = 0;
    alGetSourcei(src, AL_SAMPLE_OFFSET, &off);
    check(off >= 3072 && off <= 3072 + 1024, "sample_offset_advances");

    // Fully panned right: left much quieter than right.
    alSource3f(src, AL_POSITION, 1.0f, 0.0f, 0.0f);
    render(dev, 2048);
    o = render(dev, 1024);
    check(o[1000 * 2 + 1] > 2.0f * std::fabs(o[1000 * 2]), "pan_right");

    // Gain 0 → silence.
    alSourcef(src, AL_GAIN, 0.0f);
    render(dev, 2048);
    o = render(dev, 1024);
    check(std::fabs(o[2000]) < 1e-6f && std::fabs(o[2001]) < 1e-6f, "gain_zero_silence");

    // Plays to the end and stops.
    alSourcef(src, AL_GAIN, 1.0f);
    for(int i = 0; i < 60; ++i) render(dev, 1024);
    alGetSourcei(src, AL_SOURCE_STATE, &state);
    check(state == AL_STOPPED, "source_stops_at_end");

    // Invalid buffer data is rejected with AL_INVALID_VALUE (odd size for 16-bit).
    alGetError();
    ALuint b2 = 0;
    alGenBuffers(1, &b2);
    alBufferData(b2, AL_FORMAT_MONO16, dc.data(), 3, 44100);
    check(alGetError() == AL_INVALID_VALUE, "reject_misaligned_data");
    alBufferData(b2, AL_FORMAT_MONO16, dc.data(), 4, -1);
    check(alGetError() == AL_INVALID_VALUE, "reject_negative_rate");

    // IMA4 decode: one 36-byte mono block (65 samples) of zero nibbles with a positive
    // initial sample → constant, non-zero output, equal L/R when centered.
    unsigned char ima[36] = {};
    ima[0] = 0x00; ima[1] = 0x20;  // initial sample 8192
    alBufferData(b2, AL_FORMAT_MONO_IMA4, ima, sizeof(ima), 44100);
    check(alGetError() == AL_NO_ERROR, "ima4_load");
    ALint ilen = 0;
    alGetBufferi(b2, AL_SAMPLE_LENGTH_SOFT, &ilen);
    check(ilen == 65, "ima4_sample_length");
    alSourceStop(src);
    alSourcei(src, AL_BUFFER, ALint(b2));
    alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcei(src, AL_LOOPING, AL_TRUE);
    alSourcePlay(src);
    render(dev, 2048);
    o = render(dev, 256);
    check(std::fabs(o[200] - o[201]) < 1e-4f && o[200] > 0.05f, "ima4_render");

    // EFX: reverb slot accepts the effect; lowpass filter gain is read back.
    ALuint eff = 0, slot = 0, flt = 0;
    alGenEffects(1, &eff);
    alEffecti(eff, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
    ALint etype = 0;
    alGetEffecti(eff, AL_EFFECT_TYPE, &etype);
    check(etype == AL_EFFECT_REVERB, "effect_type_reverb");
    alEffectf(eff, AL_REVERB_DECAY_TIME, 100.0f);
    check(alGetError() == AL_INVALID_VALUE, "reverb_decay_range");
    alGenAuxiliaryEffectSlots(1, &slot);
    alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, ALint(eff));
    check(alGetError() == AL_NO_ERROR, "slot_attach");
    alGenFilters(1, &flt);
    alFilteri(flt, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
    alFilterf(flt, AL_LOWPASS_GAIN, 0.25f);
    ALfloat fg = 0.0f;
    alGetFilterf(flt, AL_LOWPASS_GAIN, &fg);
    check(fg == 0.25f, "lowpass_gain_roundtrip");

    alSourceStop(src);
    alDeleteSources(1, &src);
    alDeleteBuffers(1, &buf);
    alDeleteBuffers(1, &b2);
    alDeleteEffects(1, &eff);
    alDeleteAuxiliaryEffectSlots(1, &slot);
    alDeleteFilters(1, &flt);
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(ctx);
    check(alcCloseDevice(dev) == ALC_TRUE, "device_close");
    return g_fail ? 1 : 0;
}

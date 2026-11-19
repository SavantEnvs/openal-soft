// fuzz_effects: a byte-coded sequence of public AL/EFX calls over a small pool of objects
// (2 sources, 2 effects, 2 filters, 2 auxiliary effect slots, 1 looping buffer), with
// loopback rendering between ops. Reaches the effect/filter/slot/source property setters
// (al/effect.cpp, al/effects/*, al/filter.cpp, al/auxeffectslot.cpp, al/source.cpp,
// al/state.cpp) and the effect processors (alc/effects/*: reverb, chorus, echo, pitch
// shifter, vocal morpher, ...). Op codes are an allow-list; parameter enums come from the
// input (low 16 bits, or the effect/filter type tables) and values are raw.
// Per input a fresh loopback device + context is created and destroyed.
#include "common.h"

static const ALint kEffectTypes[] = {AL_EFFECT_NULL, AL_EFFECT_REVERB, AL_EFFECT_CHORUS,
    AL_EFFECT_DISTORTION, AL_EFFECT_ECHO, AL_EFFECT_FLANGER, AL_EFFECT_FREQUENCY_SHIFTER,
    AL_EFFECT_VOCAL_MORPHER, AL_EFFECT_PITCH_SHIFTER, AL_EFFECT_RING_MODULATOR,
    AL_EFFECT_AUTOWAH, AL_EFFECT_COMPRESSOR, AL_EFFECT_EQUALIZER, AL_EFFECT_EAXREVERB,
    AL_EFFECT_DEDICATED_LOW_FREQUENCY_EFFECT, AL_EFFECT_DEDICATED_DIALOGUE,
    0xA000 /* AL_EFFECT_CONVOLUTION_SOFT (alc/inprogext.h, in-progress extension) */};
static const ALint kFilterTypes[] = {AL_FILTER_NULL, AL_FILTER_LOWPASS,
    AL_FILTER_HIGHPASS, AL_FILTER_BANDPASS};
static const ALenum kSrcF[] = {AL_PITCH, AL_GAIN, AL_MIN_GAIN, AL_MAX_GAIN,
    AL_MAX_DISTANCE, AL_ROLLOFF_FACTOR, AL_CONE_OUTER_GAIN, AL_CONE_INNER_ANGLE,
    AL_CONE_OUTER_ANGLE, AL_REFERENCE_DISTANCE, AL_SEC_OFFSET, AL_AIR_ABSORPTION_FACTOR,
    AL_ROOM_ROLLOFF_FACTOR, AL_CONE_OUTER_GAINHF, AL_SOURCE_RADIUS, AL_STEREO_ANGLES};
static const ALenum kSrcI[] = {AL_SOURCE_RELATIVE, AL_LOOPING, AL_SAMPLE_OFFSET,
    AL_BYTE_OFFSET, AL_DIRECT_FILTER_GAINHF_AUTO, AL_AUXILIARY_SEND_FILTER_GAIN_AUTO,
    AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO, AL_DIRECT_CHANNELS_SOFT, AL_DISTANCE_MODEL,
    AL_SOURCE_RESAMPLER_SOFT, AL_SOURCE_SPATIALIZE_SOFT, AL_STEREO_MODE_SOFT,
    AL_SUPER_STEREO_WIDTH_SOFT};

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
    fz::setup_env("[general]\n");
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if(size > 4096) return -1;
    ALCdevice *dev = alcLoopbackOpenDeviceSOFT(nullptr);
    if(!dev) abort();
    const ALCint attrs[] = {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
        ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT, ALC_FREQUENCY, 44100,
        ALC_MAX_AUXILIARY_SENDS, 2, 0};
    ALCcontext *ctx = alcCreateContext(dev, attrs);
    if(!ctx || !alcMakeContextCurrent(ctx)) abort();

    static int16_t tone[512];
    for(int i = 0; i < 512; ++i) tone[i] = int16_t((i * 977) % 30000 - 15000);
    ALuint buf = 0, src[2] = {}, eff[2] = {}, flt[2] = {}, slot[2] = {};
    alGenBuffers(1, &buf);
    alBufferData(buf, AL_FORMAT_MONO16, tone, sizeof(tone), 22050);
    alGenSources(2, src);
    alGenEffects(2, eff);
    alGenFilters(2, flt);
    alGenAuxiliaryEffectSlots(2, slot);
    for(ALuint s : src)
    {
        alSourcei(s, AL_BUFFER, ALint(buf));
        alSourcei(s, AL_LOOPING, AL_TRUE);
    }
    alGetError();

    fz::Reader r{data, size};
    int ops = 0;
    while(r.n > 0 && ops++ < 256)
    {
        const uint8_t op = r.u8();
        const int i = op >> 7;
        switch(op & 0x1f)
        {
        case 0: alEffecti(eff[i], AL_EFFECT_TYPE,
            kEffectTypes[r.u8() % (sizeof(kEffectTypes)/sizeof(kEffectTypes[0]))]); break;
        case 1: { ALenum p = r.u8(); alEffectf(eff[i], p, r.f32()); break; }
        case 2: { ALenum p = r.u8(); alEffecti(eff[i], p, ALint(r.u32())); break; }
        case 3: { ALenum p = r.u8(); float v[3] = {r.f32(), r.f32(), r.f32()};
            alEffectfv(eff[i], p, v); break; }
        case 4: alFilteri(flt[i], AL_FILTER_TYPE,
            kFilterTypes[r.u8() % (sizeof(kFilterTypes)/sizeof(kFilterTypes[0]))]); break;
        case 5: { ALenum p = r.u8(); alFilterf(flt[i], p, r.f32()); break; }
        case 6: alAuxiliaryEffectSloti(slot[i], AL_EFFECTSLOT_EFFECT, ALint(eff[r.u8() & 1]));
            break;
        case 7: { ALenum p = r.u8(); alAuxiliaryEffectSlotf(slot[i], p, r.f32()); break; }
        case 8: { ALenum p = r.u8(); alAuxiliaryEffectSloti(slot[i], p, ALint(r.u32())); break; }
        case 9: { uint8_t b = r.u8();
            alSource3i(src[i], AL_AUXILIARY_SEND_FILTER, (b & 4) ? 0 : ALint(slot[b & 1]),
                (b >> 1) & 1, (b & 8) ? ALint(flt[(b >> 4) & 1]) : 0); break; }
        case 10: alSourcei(src[i], AL_DIRECT_FILTER, (r.u8() & 1) ? ALint(flt[0]) : 0); break;
        case 11: { ALenum p = kSrcF[r.u8() % (sizeof(kSrcF)/sizeof(kSrcF[0]))];
            alSourcef(src[i], p, r.f32()); break; }
        case 12: { ALenum p = kSrcI[r.u8() % (sizeof(kSrcI)/sizeof(kSrcI[0]))];
            alSourcei(src[i], p, ALint(r.u32())); break; }
        case 13: { float x = r.f32(), y = r.f32(), z = r.f32();
            alSource3f(src[i], (op & 0x20) ? AL_VELOCITY : AL_POSITION, x, y, z); break; }
        case 14: { float x = r.f32(), y = r.f32(), z = r.f32();
            alListener3f((op & 0x20) ? AL_VELOCITY : AL_POSITION, x, y, z); break; }
        case 15: { float o[6]; for(float &v : o) v = r.f32(); alListenerfv(AL_ORIENTATION, o);
            break; }
        case 16: alListenerf(AL_GAIN, r.f32()); break;
        case 17: alSourcePlay(src[i]); break;
        case 18: alSourceStop(src[i]); break;
        case 19: alSourceRewind(src[i]); break;
        case 20: alSourcePause(src[i]); break;
        case 21: alDopplerFactor(r.f32()); break;
        case 22: alSpeedOfSound(r.f32()); break;
        case 23: alDistanceModel(AL_INVERSE_DISTANCE + (r.u8() % 7)); break;
        case 24: alAuxiliaryEffectSloti(slot[i], AL_EFFECTSLOT_TARGET_SOFT,
            (r.u8() & 1) ? ALint(slot[i ^ 1]) : 0); break;
        default: fz::render(dev, 1, 64 + (op & 0x60) * 4); break;
        }
        alGetError();
    }
    alSourcePlayv(2, src);
    fz::render(dev, 2);

    alDeleteSources(2, src);
    alDeleteAuxiliaryEffectSlots(2, slot);
    alDeleteEffects(2, eff);
    alDeleteFilters(2, flt);
    alDeleteBuffers(1, &buf);
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(ctx);
    alcCloseDevice(dev);
    return 0;
}

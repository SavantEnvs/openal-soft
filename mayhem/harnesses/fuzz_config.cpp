// fuzz_config: the alsoft.conf parser (alc/alconfig.cpp ReadALConfig/LoadConfigFromFile)
// and the config consumers run at device/context creation (resampler, channels,
// sample-type, period/frequency, HRTF, stereo-encoding, filters, default reverb, effect
// exclusions). The library reads its config exactly once per process, so this is a
// file-input target: argv[1] is the config file, passed as ALSOFT_CONF before the first AL
// call (all other config search paths point at an empty scratch dir).
#include "common.h"

int main(int argc, char **argv)
{
    if(argc < 2) { fprintf(stderr, "usage: %s <alsoft.conf>\n", argv[0]); return 2; }
    fz::setup_env("");
    setenv("ALSOFT_CONF", argv[1], 1);
    unsetenv("ALSOFT_DRIVERS");

    ALCdevice *dev = alcLoopbackOpenDeviceSOFT(nullptr);
    if(!dev) return 1;
    const ALCint attrs[] = {ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
        ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT, ALC_FREQUENCY, 44100, 0};
    ALCcontext *ctx = alcCreateContext(dev, attrs);
    if(ctx && alcMakeContextCurrent(ctx))
    {
        static const int16_t tone[512] = {0, 8000, 16000, 8000, 0, -8000, -16000, -8000};
        ALuint buf = 0, src = 0;
        alGenBuffers(1, &buf);
        alBufferData(buf, AL_FORMAT_MONO16, tone, sizeof(tone), 22050);
        alGenSources(1, &src);
        alSourcei(src, AL_BUFFER, ALint(buf));
        alSource3f(src, AL_POSITION, 1.0f, 0.0f, -1.0f);
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

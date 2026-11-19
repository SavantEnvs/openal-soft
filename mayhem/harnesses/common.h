// Shared helpers for the openal-soft harnesses: a per-process scratch dir, a fixed
// alsoft.conf (set via ALSOFT_CONF before the library's one-time config read), and
// loopback-device helpers (alcLoopbackOpenDeviceSOFT + alcRenderSamplesSOFT: no audio
// hardware, deterministic rendering driven by the harness).
#pragma once
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

namespace fz {

inline std::string g_dir;

// Create a private scratch dir under $TMPDIR (fallback /tmp) and point every config
// search path at it, so no host config leaks in. `conf` is written as the only config.
inline void setup_env(const std::string &conf)
{
    const char *tmp = getenv("TMPDIR");
    std::string templ = std::string(tmp && *tmp ? tmp : "/tmp") + "/oalfuzz.XXXXXX";
    std::vector<char> buf(templ.begin(), templ.end());
    buf.push_back('\0');
    if(!mkdtemp(buf.data())) abort();
    g_dir = buf.data();
    std::string confpath = g_dir + "/alsoft.conf";
    std::string c = conf;
    // "@DIR@" in the config text expands to the scratch dir.
    for(size_t p; (p = c.find("@DIR@")) != std::string::npos;)
        c.replace(p, 5, g_dir);
    FILE *f = fopen(confpath.c_str(), "wb");
    if(!f) abort();
    fwrite(c.data(), 1, c.size(), f);
    fclose(f);
    setenv("HOME", g_dir.c_str(), 1);
    setenv("XDG_CONFIG_HOME", g_dir.c_str(), 1);
    setenv("XDG_CONFIG_DIRS", g_dir.c_str(), 1);
    setenv("XDG_DATA_HOME", g_dir.c_str(), 1);
    setenv("XDG_DATA_DIRS", g_dir.c_str(), 1);
    setenv("ALSOFT_CONF", confpath.c_str(), 1);
    setenv("ALSOFT_DRIVERS", "null", 1);
    setenv("ALSOFT_LOGLEVEL", "0", 1);
}

inline bool write_file(const std::string &path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path.c_str(), "wb");
    if(!f) return false;
    if(size) fwrite(data, 1, size, f);
    fclose(f);
    return true;
}

// Render `blocks` blocks of `frames` frames; the buffer is sized for the widest output
// (up to 16 channels of 4-byte samples).
inline void render(ALCdevice *dev, int blocks, int frames = 1024)
{
    static std::vector<float> out(1024 * 64);
    for(int i = 0; i < blocks; ++i)
        alcRenderSamplesSOFT(dev, out.data(), frames);
}

struct Reader {
    const uint8_t *p;
    size_t n;
    uint8_t u8() { if(!n) return 0; --n; return *p++; }
    uint16_t u16() { return uint16_t(u8() | (u8() << 8)); }
    uint32_t u32() { uint32_t v = u16(); return v | (uint32_t(u16()) << 16); }
    float f32() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return f; }
};

} // namespace fz

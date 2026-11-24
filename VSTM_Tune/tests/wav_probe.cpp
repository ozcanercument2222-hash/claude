//==============================================================================
//  VSTM Tune - gercek kayitlar icin cevrimdisi render + analiz kaydi (JUCE gerektirmez)
//
//    wav_probe in.wav out.wav [--csv hops.csv] [name=value ...]
//
//  name: key (-1 = Auto, 0..11), scale (0..12), amount (0..1), retune (ms),
//        humanize (0..1), robotism (0..1), vibrato (0..2), formant (st),
//        transpose (st), mix (0..1), realtime (0/1), range (0..2), block (samples),
//        custom (custom scale: note mask 0..4095, bit i = i semitones above the key),
//        adaptive (0/1), follow (Auto key: follow key changes after the lock, 0/1),
//        lock (Auto key: start locked to this tonic 0..11, like a restored session),
//        switch (toggle REALTIME <-> HQ every this many seconds while rendering, at block boundaries;
//                the printed latency is the one at the end)
//
//  CSV: her analiz adimi (~2.9 ms) icin zaman (giris zaman cizgisinde, saniye),
//  voiced, giris perdesi, cikis perdesi (MIDI), hedef nota, sapma ve bayraklar.
//  Cikis WAV'i 32-bit float, motor gecikmesi KORUNUR (host'taki gibi); sona gecikme
//  kadar sessizlik eklenir, boylece girisin son ornekleri de cikar (cikis = giris + gecikme).
//  Desteklenen giris: PCM 16 / 24 / 32 bit ve 32-bit float; bozuk / kisa chunk reddedilir.
//==============================================================================
#include "dsp/TuneEngine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace vstm;

namespace
{
struct Wav
{
    int channels = 0;
    double rate = 0.0;
    std::vector<std::vector<float>> data;
};

uint32_t rd32 (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t rd16 (const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

bool readWav (const char* path, Wav& w)
{
    FILE* f = std::fopen (path, "rb");
    if (! f) return false;
    std::vector<unsigned char> b;
    unsigned char tmp[65536];
    size_t n;
    while ((n = std::fread (tmp, 1, sizeof (tmp), f)) > 0) b.insert (b.end(), tmp, tmp + n);
    std::fclose (f);
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0) return false;

    int fmt = 0, bits = 0;
    size_t pos = 12;
    const unsigned char* pcm = nullptr;
    size_t pcmBytes = 0;
    while (pos + 8 <= b.size())
    {
        const uint32_t size = rd32 (b.data() + pos + 4);
        const size_t avail = b.size() - (pos + 8);                    // bytes really present after the header
        const unsigned char* body = b.data() + pos + 8;
        if (std::memcmp (b.data() + pos, "fmt ", 4) == 0)
        {
            if (size < 16 || avail < 16) return false;                // truncated / malformed fmt chunk
            fmt = rd16 (body);
            w.channels = rd16 (body + 2);
            w.rate = rd32 (body + 4);
            bits = rd16 (body + 14);
            if (fmt == 0xFFFE && size >= 26 && avail >= 26) fmt = rd16 (body + 24);   // WAVE_FORMAT_EXTENSIBLE sub-format
        }
        else if (std::memcmp (b.data() + pos, "data", 4) == 0)
        {
            pcm = body;
            pcmBytes = std::min ((size_t) size, avail);
        }
        pos += 8 + (size_t) size + (size & 1);
    }
    if (! pcm || w.channels <= 0 || w.rate < 8000.0 || w.rate > 768000.0) return false;
    // supported: PCM 16 / 24 / 32 bit and 32-bit float (anything else is refused before any sample is read)
    if (! ((fmt == 1 && (bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) return false;

    const int bps = bits / 8;
    const size_t frames = pcmBytes / (size_t) (bps * w.channels);
    w.data.assign ((size_t) w.channels, std::vector<float> (frames));
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < w.channels; ++c)
        {
            const unsigned char* s = pcm + (i * (size_t) w.channels + (size_t) c) * (size_t) bps;
            float v = 0.0f;
            if (fmt == 3 && bits == 32)      { float x; std::memcpy (&x, s, 4); v = x; }
            else if (bits == 16)             v = (int16_t) rd16 (s) / 32768.0f;
            else if (bits == 24)             { int32_t x = (int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24); v = (float) (x / 2147483648.0); }
            else if (bits == 32)             v = (float) ((int32_t) rd32 (s) / 2147483648.0);
            else return false;
            w.data[(size_t) c][i] = v;
        }
    return true;
}

bool writeWav (const char* path, const Wav& w)
{
    FILE* f = std::fopen (path, "wb");
    if (! f) return false;
    const uint32_t frames = (uint32_t) w.data[0].size();
    const uint32_t dataBytes = frames * (uint32_t) w.channels * 4;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (3); w16 ((uint16_t) w.channels);
    w32 ((uint32_t) w.rate); w32 ((uint32_t) w.rate * (uint32_t) w.channels * 4); w16 ((uint16_t) (w.channels * 4)); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (int c = 0; c < w.channels; ++c)
            std::fwrite (&w.data[(size_t) c][i], 4, 1, f);
    std::fclose (f);
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: wav_probe in.wav out.wav [--csv hops.csv] [name=value ...]\n");
        return 2;
    }
    Wav w;
    if (! readWav (argv[1], w)) { std::fprintf (stderr, "cannot read %s (PCM 16/24/32 or float32 WAV)\n", argv[1]); return 3; }

    TuneEngine::Params p;
    bool realtime = true;
    int block = 512;
    int lockKey = -1;
    double switchEvery = 0.0;
    const char* csvPath = nullptr;
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--csv" && i + 1 < argc) { csvPath = argv[++i]; continue; }
        const auto eq = a.find ('=');
        if (eq == std::string::npos) { std::fprintf (stderr, "bad arg %s\n", a.c_str()); return 2; }
        const std::string k = a.substr (0, eq);
        const double v = std::atof (a.c_str() + eq + 1);
        if      (k == "key")       p.key = (int) v;
        else if (k == "scale")     p.scale = (int) v;
        else if (k == "amount")    p.amount = (float) v;
        else if (k == "retune")    p.retuneMs = (float) v;
        else if (k == "humanize")  p.humanize = (float) v;
        else if (k == "robotism")  p.robotism = (float) v;
        else if (k == "vibrato")   p.vibratoScale = (float) v;
        else if (k == "formant")   p.formant = (float) v;
        else if (k == "transpose") p.transpose = (float) v;
        else if (k == "mix")       p.mix = (float) v;
        else if (k == "range")     p.range = (int) v;
        else if (k == "custom")    { p.customScale = true; p.customMask = (uint16_t) ((int) v & 0x0fff); }
        else if (k == "adaptive")  p.adaptive = v != 0.0;
        else if (k == "follow")    p.followKeyChanges = v != 0.0;
        else if (k == "guard")     p.peakGuard = v != 0.0;
        else if (k == "backdate")  p.backdateSwitch = v != 0.0;
        else if (k == "lock")      lockKey = (int) v;
        else if (k == "realtime")  realtime = v != 0.0;
        else if (k == "block")     block = std::max (1, (int) v);
        else if (k == "switch")    switchEvery = std::max (0.0, v);
        else { std::fprintf (stderr, "unknown param %s\n", k.c_str()); return 2; }
    }

    TuneEngine e;
    e.prepare (w.rate, realtime);
    e.setParams (p);
    if (lockKey >= 0)
        e.requestKeyLock (lockKey, 1.0f);
    const int nc = std::min (2, w.channels);
    const int latency = e.getLatencySamples();
    const size_t inFrames = w.data[0].size();
    for (auto& c : w.data)                                  // flush the engine's delay: the end of the input comes out too
        c.resize (inFrames + (size_t) latency, 0.0f);
    const size_t frames = w.data[0].size();

    FILE* csv = csvPath ? std::fopen (csvPath, "w") : nullptr;
    if (csv) std::fprintf (csv, "time_s,voiced,in_midi,out_midi,note,deviation,note_change,settled,auto_key,key_state,key_conf,est_delay_s,clarity\n");
    const double hopS = e.getHopSeconds();
    const double latS = e.getLatencySamples() / w.rate;
    int64_t hopIndex = 0;

    long switches = 0;
    for (size_t pos = 0; pos < frames; pos += (size_t) block)
    {
        if (switchEvery > 0.0 && (double) pos >= (double) (switches + 1) * switchEvery * w.rate)
        {
            realtime = ! realtime;
            e.setRealtime (realtime);
            ++switches;
        }
        const int n = (int) std::min ((size_t) block, frames - pos);
        float* ch[2] = { w.data[0].data() + pos, nc > 1 ? w.data[1].data() + pos : nullptr };
        e.process (ch, nc, n);
        TuneEngine::DisplayPoint d;
        while (e.display.pop (d))
        {
            ++hopIndex;
            if (csv)
                std::fprintf (csv, "%.5f,%d,%.4f,%.4f,%d,%.4f,%d,%d,%d,%d,%.3f,%.5f,%.4f\n", hopIndex * hopS, (d.flags & TuneEngine::kVoiced) ? 1 : 0,
                              d.inMidi, d.outMidi, (int) d.note, d.deviation,
                              (d.flags & TuneEngine::kNoteChange) ? 1 : 0, (d.flags & TuneEngine::kSettled) ? 1 : 0,
                              e.detectedKey.load(), e.keyState.load(), e.keyConfidence.load(), d.analysisDelay, d.clarity);
        }
    }
    if (csv) std::fclose (csv);

    Wav out = w;
    out.channels = nc;
    out.data.resize ((size_t) nc);
    if (! writeWav (argv[2], out)) { std::fprintf (stderr, "cannot write %s\n", argv[2]); return 4; }
    std::printf ("rendered %zu frames (+%d flush) @ %.0f Hz, latency %d samples (%.1f ms), hop %.2f ms, guard_min %.6f\n",
                 inFrames, latency, w.rate, latency, latS * 1000.0, hopS * 1000.0, (double) e.guardMin);
    return 0;
}

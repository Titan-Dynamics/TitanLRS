#include "rf_diag.h"

#if defined(PLATFORM_STM32)

#include <math.h>
#include <stdio.h>

#include "common.h"
#include "config.h"
#include "FHSS.h"
#include "POWERMGNT.h"
#include "hwTimer.h"
#include "logging.h"
#include "OTA.h"
#include "rxtx_intf.h"

#if defined(RADIO_LR2021)
#include "LR2021_Regs.h"
#define RF_DIAG_SUPPORTED 1
#endif

#if defined(RF_DIAG_SUPPORTED)

enum Mode : uint8_t
{
    MODE_OFF,
    MODE_RSSI,
    MODE_CW,
};

struct Settings
{
    Mode mode;
    bool highBand;      // false: FHSSconfig (sub-GHz), true: FHSSconfigDualBand (2.4 GHz)
    bool hop;           // RSSI: walk the band's FHSS sequence instead of sitting on `channel`
    uint8_t channel;
    uint8_t radios;     // SX12XX_Radio_Number_t mask; exactly one radio for CW
    uint32_t bandwidth; // RSSI receive bandwidth, Hz
    PowerLevels_e power;
    bool raw;           // CW: rawPower instead of the power table entry for `power`
    int8_t rawPower;    // radio output setting, 0.5 dB steps
};

struct Bandwidth
{
    uint32_t hz;
    uint8_t reg;
    bool lowBand;
    bool highBand;
};

static const Bandwidth bandwidths[] = {
    {62500, LR2021_RADIO_LORA_BW_62, true, true},
    {500000, LR2021_RADIO_LORA_BW_500, true, true},
    {812000, LR2021_RADIO_LORA_BW_800, false, true},
};

static const uint16_t powerLevelMw[PWR_COUNT] = {10, 25, 50, 100, 250, 500, 1000, 2000};

struct Sample
{
    int16_t rssi[2];
    uint8_t channel;
};

static constexpr uint32_t SAMPLE_PERIOD_US = 1000;
// The browser polls about every 100 ms, but a hidden tab can be throttled to one request a
// second; the ring rides that out on the H7. The F4's smaller lwIP heap also limits how much
// one response may carry.
#if defined(STM32H7xx)
static constexpr uint32_t RING_LEN = 4096;
static constexpr uint32_t MAX_SAMPLES_PER_RESPONSE = 400;
#else
static constexpr uint32_t RING_LEN = 1024;
static constexpr uint32_t MAX_SAMPLES_PER_RESPONSE = 200;
#endif
// "-128," twice plus "255," per sample, and the fixed fields around them.
static constexpr size_t SAMPLE_JSON_MAX = 14;
static constexpr size_t SAMPLES_HEADER_MAX = 192;
static char samplesJson[SAMPLES_HEADER_MAX + MAX_SAMPLES_PER_RESPONSE * SAMPLE_JSON_MAX];

static Settings s = {};
static Sample ring[RING_LEN];
static uint32_t sampleSeq = 0;   // sequence number of the next sample
static uint32_t settingsSeq = 0; // first sample taken with the current settings
static uint32_t lastSampleUs = 0;
static uint16_t hopIndex = 0;
static uint8_t tunedChannel = 0;

static const fhss_config_t *bandConfig(bool high)
{
    return high ? FHSSconfigDualBand : FHSSconfig;
}

// The band is offered when the hardware layout has a power table for it, which is also what the
// configurator's has_low_band / has_high_band report.
static bool bandAvailable(bool high)
{
    return high ? POWER_OUTPUT_VALUES_DUAL_COUNT != 0 : POWER_OUTPUT_VALUES_COUNT != 0;
}

static uint32_t channelFreq(bool high, uint8_t channel)
{
    const uint32_t spread = high ? freq_spread_DualBand : freq_spread;
    return bandConfig(high)->freq_start + spread * channel / FREQ_SPREAD_SCALE;
}

static uint16_t sequenceLength(bool high)
{
    return high ? secondaryBandCount : primaryBandCount;
}

static uint8_t sequenceChannel(bool high, uint16_t index)
{
    return high ? FHSSsequence_DualBand[index] : FHSSsequence[index];
}

static const Bandwidth *findBandwidth(uint32_t hz, bool high)
{
    for (const Bandwidth &bw : bandwidths)
    {
        if (bw.hz == hz && (high ? bw.highBand : bw.lowBand))
        {
            return &bw;
        }
    }
    return nullptr;
}

// What the link itself receives with: 500 kHz LoRa on sub-GHz, 812 kHz on 2.4 GHz.
static uint32_t defaultBandwidth(bool high)
{
    return high ? 812000 : 500000;
}

static int8_t rawPowerMin(bool high)
{
    return high ? LR2021_POWER_MIN_HF_PA : LR2021_POWER_MIN_LF_PA;
}

static int8_t rawPowerMax(bool high)
{
    return high ? LR2021_POWER_MAX_HF_PA : LR2021_POWER_MAX_LF_PA;
}

static int8_t tablePower(bool high, PowerLevels_e level)
{
    const int16_t *values = high ? POWER_OUTPUT_VALUES_DUAL : POWER_OUTPUT_VALUES;
    const uint8_t count = high ? POWER_OUTPUT_VALUES_DUAL_COUNT : POWER_OUTPUT_VALUES_COUNT;
    uint8_t idx = level - POWERMGNT::getMinPower();
    if (idx >= count)
    {
        idx = count - 1;
    }
    return values[idx];
}

static uint8_t availableRadios()
{
    return isDualRadio() ? SX12XX_Radio_All : SX12XX_Radio_1;
}

static Settings defaults()
{
    Settings d = {};
    d.mode = MODE_OFF;
    d.highBand = !bandAvailable(false);
    d.hop = false;
    d.channel = bandConfig(d.highBand)->freq_count / 2;
    d.radios = SX12XX_Radio_1;
    d.bandwidth = defaultBandwidth(d.highBand);
    d.power = POWERMGNT::getMinPower();
    d.raw = false;
    d.rawPower = 0;
    return d;
}

static const char *modeName(Mode mode)
{
    switch (mode)
    {
    case MODE_RSSI:
        return "rssi";
    case MODE_CW:
        return "cw";
    default:
        return "off";
    }
}

// A link that is up and armed must not be dropped from here. The RX only knows the arm state
// while the TX is sending it.
static bool linkIsArmed()
{
#if defined(TARGET_TX)
    return isArmed;
#else
    return connectionState == connected && isArmed;
#endif
}

static void stopLink()
{
    hwTimer::stop();
    Radio.RXdoneCallback = &SX12xxDriverCommon::nullCallbackRx;
    Radio.TXdoneCallback = &SX12xxDriverCommon::nullCallbackTx;
    Radio.DisableIsr();
    setConnectionState(rfDiag);
    DBGLN("RF diagnostics: link stopped");
}

static void tune(uint8_t channel)
{
    tunedChannel = channel;
    Radio.SetTxIdleMode();
    Radio.SetFrequencyReg(channelFreq(s.highBand, channel), s.radios, true);
}

static void startRssi()
{
    const Bandwidth *bw = findBandwidth(s.bandwidth, s.highBand);
    const uint8_t channel = s.hop ? sequenceChannel(s.highBand, 0) : s.channel;
    const uint32_t freq = channelFreq(s.highBand, channel);
    const RadioBandMod::Combined modulation = s.highBand ? RadioBandMod::LORA_2G4 : RadioBandMod::LORA_900;
    // Only the receive bandwidth matters for RSSI; the rest is a valid LoRa setup for either band.
    Radio.Config(bw->reg, LR2021_RADIO_LORA_SF6, LR2021_RADIO_LORA_CR_4_8, freq, 12, false, 8, modulation,
                 0, 0, 0, 0, availableRadios());
    hopIndex = 0;
    tune(channel);
}

static void startCw()
{
    // POWERMGNT keeps the bookkeeping (fan threshold, reported power) in step; the radio's
    // output setting for this band is then written directly, so a raw setting and a table
    // entry that happens to be the current level both take effect.
    POWERMGNT::setPower(s.raw ? POWERMGNT::getMaxPower() : s.power);
    Radio.SetOutputPower(s.raw ? s.rawPower : tablePower(s.highBand, s.power), !s.highBand);
    // startCWTest() only touches the radio it is given; the other must not keep a carrier from
    // an earlier run on it, or keep receiving.
    Radio.SetTxIdleMode();
    Radio.startCWTest(channelFreq(s.highBand, s.channel), s.radios);
}

bool RfDiag_Supported()
{
    return true;
}

bool RfDiag_Active()
{
    return connectionState == rfDiag;
}

static void describeBand(JsonArray bands, bool high)
{
    const fhss_config_t *cfg = bandConfig(high);
    JsonObject band = bands.add<JsonObject>();
    band["id"] = high ? "high" : "low";
    band["domain"] = cfg->domain;
    band["start"] = cfg->freq_start;
    band["stop"] = cfg->freq_stop;
    band["spread"] = (high ? freq_spread_DualBand : freq_spread) / FREQ_SPREAD_SCALE;
    band["count"] = cfg->freq_count;
    band["center"] = cfg->freq_count / 2;
    band["sequence"] = sequenceLength(high);
    JsonArray bws = band["bandwidths"].to<JsonArray>();
    for (const Bandwidth &bw : bandwidths)
    {
        if (high ? bw.highBand : bw.lowBand)
        {
            bws.add(bw.hz);
        }
    }
    band["default-bandwidth"] = defaultBandwidth(high);
    JsonObject raw = band["raw-power"].to<JsonObject>();
    raw["min"] = rawPowerMin(high) / 2.0f;
    raw["max"] = rawPowerMax(high) / 2.0f;
    raw["step"] = 0.5f;
    JsonArray table = band["power-values"].to<JsonArray>();
    for (int level = POWERMGNT::getMinPower(); level <= POWERMGNT::getMaxPower(); level++)
    {
        table.add(tablePower(high, (PowerLevels_e)level) / 2.0f);
    }
}

void RfDiag_Describe(JsonObject out)
{
    out["active"] = RfDiag_Active();
    if (!RfDiag_Active() && linkIsArmed())
    {
        out["blocked"] = "module is armed";
    }
    out["radios"] = isDualRadio() ? 2 : 1;
    out["sample-period-us"] = SAMPLE_PERIOD_US;

    JsonArray bands = out["bands"].to<JsonArray>();
    if (bandAvailable(false))
    {
        describeBand(bands, false);
    }
    if (bandAvailable(true))
    {
        describeBand(bands, true);
    }

    JsonArray levels = out["power-levels"].to<JsonArray>();
    for (int level = POWERMGNT::getMinPower(); level <= POWERMGNT::getMaxPower(); level++)
    {
        JsonObject entry = levels.add<JsonObject>();
        entry["level"] = level;
        entry["mw"] = powerLevelMw[level];
    }

    const Settings cur = s.mode == MODE_OFF ? defaults() : s;
    JsonObject state = out["state"].to<JsonObject>();
    state["mode"] = modeName(cur.mode);
    state["band"] = cur.highBand ? "high" : "low";
    state["channel"] = cur.channel;
    state["freq"] = channelFreq(cur.highBand, cur.channel);
    state["hop"] = cur.hop;
    state["radio"] = cur.radios;
    state["bandwidth"] = cur.bandwidth;
    state["power"] = cur.power;
    if (cur.raw)
    {
        state["raw-power"] = cur.rawPower / 2.0f;
    }
    else
    {
        state["raw-power"] = nullptr;
    }
    state["since"] = settingsSeq;
}

const char *RfDiag_Apply(JsonVariantConst req, int &status)
{
    status = 400;
    if (!RfDiag_Active() && linkIsArmed())
    {
        status = 409;
        return "module is armed";
    }

    Settings next = s.mode == MODE_OFF ? defaults() : s;

    const char *mode = req["mode"] | "";
    if (strcmp(mode, "rssi") == 0)
        next.mode = MODE_RSSI;
    else if (strcmp(mode, "cw") == 0)
        next.mode = MODE_CW;
    else
        return "mode must be \"rssi\" or \"cw\"";

    if (req["band"].is<const char *>())
    {
        const char *band = req["band"];
        if (strcmp(band, "low") != 0 && strcmp(band, "high") != 0)
            return "band must be \"low\" or \"high\"";
        const bool high = strcmp(band, "high") == 0;
        if (!bandAvailable(high))
            return "band not available on this hardware";
        if (high != next.highBand)
        {
            // Channel numbers and bandwidths are per band; start the new band from its centre.
            next.highBand = high;
            next.channel = bandConfig(high)->freq_count / 2;
            if (!findBandwidth(next.bandwidth, high))
                next.bandwidth = defaultBandwidth(high);
        }
    }

    if (req["channel"].is<int>())
    {
        const int channel = req["channel"];
        if (channel < 0 || (uint32_t)channel >= bandConfig(next.highBand)->freq_count)
            return "channel out of range";
        next.channel = channel;
    }

    if (req["hop"].is<bool>())
    {
        next.hop = req["hop"];
    }

    if (req["radio"].is<int>())
    {
        const int radios = req["radio"];
        if (radios < 1 || radios > 3 || (radios & ~availableRadios()))
            return "radio not available";
        next.radios = radios;
    }
    if (next.mode == MODE_CW && next.radios == SX12XX_Radio_All)
    {
        if (req["radio"].is<int>())
            return "continuous wave runs on one radio at a time";
        next.radios = SX12XX_Radio_1;
    }

    if (req["bandwidth"].is<uint32_t>())
    {
        next.bandwidth = req["bandwidth"];
    }
    if (!findBandwidth(next.bandwidth, next.highBand))
        return "bandwidth not available on this band";

    if (req["power"].is<int>())
    {
        const int power = req["power"];
        if (power < POWERMGNT::getMinPower() || power > POWERMGNT::getMaxPower())
            return "power level out of range";
        next.power = (PowerLevels_e)power;
    }

    if (req["raw-power"].isNull())
    {
        if (!req["raw-power"].isUnbound())
            next.raw = false;
    }
    else if (req["raw-power"].is<float>())
    {
        const int raw = (int)lroundf(req["raw-power"].as<float>() * 2.0f);
        if (raw < rawPowerMin(next.highBand) || raw > rawPowerMax(next.highBand))
            return "raw power out of range";
        next.raw = true;
        next.rawPower = raw;
    }
    else
    {
        return "raw-power must be a number or null";
    }
    if (next.raw && (next.rawPower < rawPowerMin(next.highBand) || next.rawPower > rawPowerMax(next.highBand)))
    {
        next.raw = false;
    }

    if (!RfDiag_Active())
    {
        stopLink();
    }

    s = next;
    settingsSeq = sampleSeq;
    lastSampleUs = micros();
    if (s.mode == MODE_RSSI)
        startRssi();
    else
        startCw();

    DBGLN("RF diagnostics: %s band=%s ch=%u radios=%u", modeName(s.mode), s.highBand ? "high" : "low",
          (unsigned)s.channel, (unsigned)s.radios);
    status = 200;
    return nullptr;
}

// Appends with snprintf semantics; once the buffer is full every later call is a no-op and the
// caller sees pos >= len.
static void put(char *buf, size_t len, size_t &pos, const char *fmt, int value)
{
    if (pos < len)
    {
        const int n = snprintf(buf + pos, len - pos, fmt, value);
        pos += n > 0 ? (size_t)n : 0;
    }
}

static void putSeries(char *buf, size_t len, size_t &pos, const char *name, uint32_t first, uint32_t count, int field)
{
    if (pos < len)
    {
        const int n = snprintf(buf + pos, len - pos, ",\"%s\":[", name);
        pos += n > 0 ? (size_t)n : 0;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        const Sample &smp = ring[(first + i) % RING_LEN];
        const int value = field < 2 ? smp.rssi[field] : smp.channel;
        put(buf, len, pos, i == 0 ? "%d" : ",%d", value);
    }
    put(buf, len, pos, "]", 0);
}

size_t RfDiag_Samples(uint32_t from, const char **json)
{
    char *buf = samplesJson;
    const size_t len = sizeof(samplesJson);
    *json = buf;
    // Sequence numbers are compared through differences so that the counter may wrap.
    uint32_t lost = 0;
    if ((int32_t)(sampleSeq - from) < 0)
    {
        from = sampleSeq; // the client is ahead, e.g. after a reboot: restart from now
    }
    if (sampleSeq - from > RING_LEN)
    {
        lost = sampleSeq - from - RING_LEN;
        from = sampleSeq - RING_LEN;
    }
    uint32_t count = sampleSeq - from;
    const bool more = count > MAX_SAMPLES_PER_RESPONSE;
    if (more)
    {
        count = MAX_SAMPLES_PER_RESPONSE;
    }
    const bool rssi = s.mode == MODE_RSSI;
    if (!rssi)
    {
        count = 0;
    }

    size_t pos = 0;
    const int n = snprintf(buf, len,
                           "{\"mode\":\"%s\",\"from\":%u,\"next\":%u,\"since\":%u,\"lost\":%u,\"more\":%s,\"period\":%u,\"hop\":%s",
                           modeName(s.mode), (unsigned)from, (unsigned)(from + count), (unsigned)settingsSeq,
                           (unsigned)lost, more && rssi ? "true" : "false", (unsigned)SAMPLE_PERIOD_US,
                           s.hop ? "true" : "false");
    if (n <= 0 || (size_t)n >= len)
    {
        return 0; // cannot happen: the buffer is sized for the longest header
    }
    pos = n;
    if (rssi && s.hop)
    {
        putSeries(buf, len, pos, "ch", from, count, 2);
    }
    if (rssi && (s.radios & SX12XX_Radio_1))
    {
        putSeries(buf, len, pos, "r1", from, count, 0);
    }
    if (rssi && (s.radios & SX12XX_Radio_2))
    {
        putSeries(buf, len, pos, "r2", from, count, 1);
    }
    put(buf, len, pos, "}", 0);
    return pos < len ? pos : 0;
}

void RfDiag_End()
{
    if (config.IsModified())
    {
        config.Commit();
    }
    scheduleRebootTime(100);
}

void RfDiag_Service()
{
    if (s.mode != MODE_RSSI || connectionState != rfDiag)
    {
        return;
    }
    const uint32_t now = micros();
    if (now - lastSampleUs < SAMPLE_PERIOD_US)
    {
        return;
    }
    // Keep the cadence when the loop is merely late; resynchronise after a long stall.
    lastSampleUs = (now - lastSampleUs < 2 * SAMPLE_PERIOD_US) ? lastSampleUs + SAMPLE_PERIOD_US : now;

    Radio.StartRssiInst(s.radios);
    Sample &smp = ring[sampleSeq % RING_LEN];
    smp.channel = tunedChannel;
    smp.rssi[0] = (s.radios & SX12XX_Radio_1) ? Radio.GetRssiInstDbm(SX12XX_Radio_1) : 0;
    smp.rssi[1] = (s.radios & SX12XX_Radio_2) ? Radio.GetRssiInstDbm(SX12XX_Radio_2) : 0;
    sampleSeq++;

    if (s.hop)
    {
        // The next sample is a full period away, which leaves the synthesiser and the RSSI
        // filter ample time to settle on the new channel.
        hopIndex = (hopIndex + 1) % sequenceLength(s.highBand);
        tune(sequenceChannel(s.highBand, hopIndex));
    }
}

#else // !RF_DIAG_SUPPORTED

bool RfDiag_Supported() { return false; }
bool RfDiag_Active() { return false; }
void RfDiag_Describe(JsonObject out) { out["active"] = false; }
const char *RfDiag_Apply(JsonVariantConst, int &status)
{
    status = 404;
    return "RF diagnostics not supported on this radio";
}
size_t RfDiag_Samples(uint32_t, const char **json)
{
    *json = "{}";
    return 2;
}
void RfDiag_End() {}
void RfDiag_Service() {}

#endif // RF_DIAG_SUPPORTED

#endif // PLATFORM_STM32

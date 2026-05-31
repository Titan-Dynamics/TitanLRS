#include "LR2021.h"
#include "LR2021_hal.h"
#include "FEC.h"
#include "logging.h"

LR2021Hal hal;
LR2021Driver *LR2021Driver::instance = NULL;

//DEBUG_LR2021_OTA_TIMING

#if defined(DEBUG_LR2021_OTA_TIMING)
static uint32_t beginTX;
static uint32_t endTX;
#endif

class FECCodec final : public BufferCodec
{
public:
    void encode(uint8_t *out, uint8_t *in, uint32_t len) override;
    void decode(uint8_t *out, uint8_t *in, uint32_t len) override;
} fecCodec;

class CopyCodec final : public BufferCodec
{
public:
    void encode(uint8_t *out, uint8_t *in, uint32_t len) override;
    void decode(uint8_t *out, uint8_t *in, uint32_t len) override;
} copyCodec;

void ICACHE_RAM_ATTR FECCodec::encode(uint8_t *out, uint8_t *in, const uint32_t len)
{
    memset(out, 0, len); // ensure that the buffer is zeroed to start
    FECEncode(in, out);
}

void ICACHE_RAM_ATTR FECCodec::decode(uint8_t *out, uint8_t *in, const uint32_t len)
{
    FECDecode(in, out);
}

void ICACHE_RAM_ATTR CopyCodec::encode(uint8_t *out, uint8_t *in, const uint32_t len)
{
    memcpy(out, in, len);
}
void ICACHE_RAM_ATTR CopyCodec::decode(uint8_t *out, uint8_t *in, const uint32_t len)
{
    memcpy(out, in, len);
}

LR2021Driver::LR2021Driver(): SX12xxDriverCommon()
{
    useFSK = false;
    instance = this;
    strongestReceivingRadio = SX12XX_Radio_1;
    fallBackMode = LR2021_MODE_FS;
    codec = &copyCodec;
}

void LR2021Driver::End()
{
    SetMode(LR2021_MODE_SLEEP, SX12XX_Radio_All);
    hal.end();
    RemoveCallbacks();
}

bool LR2021Driver::CheckVersion(const SX12XX_Radio_Number_t radioNumber)
{
    uint8_t buffer[4] {};
    hal.WriteCommand(LR2021_SYSTEM_GET_VERSION_OC, radioNumber);
    hal.ReadCommand(buffer, sizeof(buffer), radioNumber);
    hal.WaitOnBusy(radioNumber);

    const uint16_t version = static_cast<uint16_t>(buffer[2] << 8 | buffer[3]);
    if (version != 0x0118)
    {
        DBGLN("LR2021 #%d failed to be detected %x.", radioNumber, version);
        return false;
    }
    DBGLN("LR2021 #%d Ready", radioNumber);
    return true;
}

bool LR2021Driver::Begin(const uint32_t lowBandFreq, const uint32_t highBandFreq)
{
    hal.init();
    hal.reset();

    // Validate that the LR2021(s) are working.
    if (!CheckVersion(SX12XX_Radio_1))
        return false;
    if (GPIO_PIN_NSS_2 != UNDEF_PIN && !CheckVersion(SX12XX_Radio_2))
        return false;

    hal.IsrCallback_1 = &LR2021Driver::IsrCallback_1;
    hal.IsrCallback_2 = &LR2021Driver::IsrCallback_2;

    // Clear Errors
    hal.WriteCommand(LR2021_SYSTEM_CLEAR_ERRORS_OC, SX12XX_Radio_All);

    // 6.3.7 SetRxTxFallbackMode
    constexpr uint8_t FBbuf = LR2021_RADIO_FALLBACK_FS;
    fallBackMode = LR2021_MODE_FS;
    hal.WriteCommand(LR2021_RADIO_SET_RX_TX_FALLBACK_MODE_OC, &FBbuf, 1, SX12XX_Radio_All);

    // 6.3.17 SetDefaultRxTxTimeout - continuous RX (0xFFFFFF), no TX timeout
    constexpr uint8_t timeouts[] {
        0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00,
    };
    hal.WriteCommand(LR2021_RADIO_SET_DEFAULT_RX_TX_TIMEOUT_OC, timeouts, sizeof(timeouts), SX12XX_Radio_All);

    SetDioAsRfSwitch();

    // 6.3.18 SetRegMode
    const uint8_t RegMode = OPT_USE_HARDWARE_DCDC ? 0x02 : 0x00;
    hal.WriteCommand(LR2021_SYSTEM_SET_REGMODE_OC, &RegMode, 1, SX12XX_Radio_All); // Enable DCDC converter instead of LDO

    // 6.4.2 Calibrate
    constexpr uint8_t calibrate = 0x7F;
    hal.WriteCommand(LR2021_SYSTEM_CALIBRATE_OC, &calibrate, 1, SX12XX_Radio_All);

    // 6.4.2 CalibFE
    const uint8_t calibrateFE[]{static_cast<uint8_t>((lowBandFreq / 4000000) >> 8), static_cast<uint8_t>(lowBandFreq / 4000000), static_cast<uint8_t>(((highBandFreq / 4000000) >> 8) | 0x80), static_cast<uint8_t>(highBandFreq / 4000000)};
    hal.WriteCommand(LR2021_SYSTEM_CALIBRATE_FRONTEND_OC, calibrateFE, sizeof(calibrateFE), SX12XX_Radio_All);

    // CalibFE takes time on the BUSY line; wait for it to finish (up to 500ms)
    // before returning so that Config() is never called while calibration is
    // still running.
    {
        uint32_t deadline = millis() + 500;
        while (!hal.WaitOnBusy(SX12XX_Radio_All))
        {
            if ((int32_t)(millis() - deadline) >= 0) break;
            delay(5);
        }
    }

    return true;
}

// 12.2.1 SetTxCw
void LR2021Driver::startCWTest(const uint32_t freq, const SX12XX_Radio_Number_t radioNumber)
{
    // Set a basic Config that can be used for both 2.4G and SubGHz bands.
    Config(LR2021_RADIO_LORA_BW_62, LR2021_RADIO_LORA_SF6, LR2021_RADIO_LORA_CR_4_8, freq, 12, false, 8, false, 0, 0, radioNumber);
    constexpr uint8_t mode = 0x02;
    hal.WriteCommand(LR2021_RADIO_SET_TX_TEST_MODE_OC, &mode, 1, radioNumber);
}

void LR2021Driver::Config(const uint8_t bw, const uint8_t sf, const uint8_t cr, const uint32_t regfreq,
                          const uint8_t PreambleLength, const bool InvertIQ, const uint8_t _PayloadLength,
                          const bool setFSKModulation, const uint8_t fskSyncWord1, const uint8_t fskSyncWord2,
                          const SX12XX_Radio_Number_t radioNumber)
{
    PayloadLength = _PayloadLength;
    useFSK = setFSKModulation;

    const bool isSubGHz = regfreq < 1000000000;

    if (radioNumber & SX12XX_Radio_1)
    {
        radio1isSubGHz = isSubGHz;
    }

    if (radioNumber & SX12XX_Radio_2)
    {
        radio2isSubGHz = isSubGHz;
    }

    IQinverted = InvertIQ;
    lr20xx_radio_lora_iq_t inverted = InvertIQ ? LR2021_RADIO_LORA_IQ_INVERTED : LR2021_RADIO_LORA_IQ_STANDARD;
    // IQinverted is always STANDARD for 900
    if (isSubGHz)
    {
        inverted = LR2021_RADIO_LORA_IQ_STANDARD;
    }

    SetMode(LR2021_MODE_STDBY_RC, radioNumber);

    codec = &copyCodec;
    if (useFSK)
    {
        DBGLN("Config FSK");
        const uint32_t bitrate = static_cast<uint32_t>(bw) * 10000;
        const uint8_t bwf = sf;
        const uint32_t fdev = static_cast<uint32_t>(cr) * 1000;
        ConfigModParamsFSK(bitrate, bwf, fdev, radioNumber);

        // Increase packet length for FEC used only on 1000Hz 2.4GHz.
        if (!isSubGHz)
        {
            codec = &fecCodec;
            PayloadLength = 14;
        }

        SetPacketParamsFSK(PreambleLength, radioNumber);
        SetFSKSyncWord(fskSyncWord1, fskSyncWord2, radioNumber);
    }
    else
    {
        DBGLN("Config LoRa");
        ConfigModParamsLoRa(bw, sf, cr, radioNumber);
        SetPacketParamsLoRa(PreambleLength, inverted, radioNumber);
    }

    SetFrequencyReg(regfreq, radioNumber, false);

    // 7.2.2 SetRxPath
    if (isSubGHz)
    {
        constexpr uint8_t buf[] {0x00, 0x00};
        hal.WriteCommand(LR2021_RADIO_SET_RX_PATH_OC, buf, sizeof(buf), radioNumber);
    }
    else
    {
        constexpr uint8_t buf[] {0x01, 0x04};
        hal.WriteCommand(LR2021_RADIO_SET_RX_PATH_OC, buf, sizeof(buf), radioNumber);
    }

    ClearIrqStatus(radioNumber);

    SetPaConfig(isSubGHz, radioNumber); // Must be called after changing rf modes between subG and 2.4G.  This sets the correct rf amps to be used.
    pwrForceUpdate = true;              // force an update of the output power because the band may have changed, and we need to configure the power for the band.
    CommitOutputPower();

    hal.WriteCommand(LR2021_SYSTEM_CLEAR_RX_FIFO_OC, radioNumber);
    hal.WriteCommand(LR2021_SYSTEM_CLEAR_TX_FIFO_OC, radioNumber);
}

void LR2021Driver::ConfigModParamsLoRa(const uint8_t bw, const uint8_t sf, const uint8_t cr, const SX12XX_Radio_Number_t radioNumber)
{
    // 8.1.1 SetPacketType
    constexpr uint8_t packetType = LR2021_RADIO_PKT_TYPE_LORA;
    hal.WriteCommand(LR2021_RADIO_SET_PKT_TYPE_OC, &packetType, 1, radioNumber);

    // 8.3.1 SetModulationParams
    const uint8_t buf[] {
        static_cast<uint8_t>(sf << 4 | bw),
        static_cast<uint8_t>(cr << 4),
    };
    hal.WriteCommand(LR2021_RADIO_SET_LORA_MODULATION_PARAM_OC, buf, sizeof(buf), radioNumber);

    if (radioNumber & SX12XX_Radio_1 && radio1isSubGHz)
    {
        CorrectRegisterForSF6(sf, SX12XX_Radio_1);
    }

    if (GPIO_PIN_NSS_2 != UNDEF_PIN)
    {
        if (radioNumber & SX12XX_Radio_2 && radio2isSubGHz)
        {
            CorrectRegisterForSF6(sf, SX12XX_Radio_2);
        }
    }
}

void LR2021Driver::SetPacketParamsLoRa(const uint8_t PreambleLength, const uint8_t InvertIQ, const SX12XX_Radio_Number_t radioNumber)
{
#if defined(DEBUG_FREQ_CORRECTION)
    constexpr lr20xx_RadioLoRaPacketLengthsModes_t packetLengthType = LR2021_LORA_PACKET_EXPLICIT;
#else
    constexpr lr20xx_RadioLoRaPacketLengthsModes_t packetLengthType = LR2021_LORA_PACKET_IMPLICIT;
#endif

    // 8.3.2 SetPacketParams
    const uint8_t buf[] {
        static_cast<uint8_t>(PreambleLength >> 8),   // MSB PbLengthTX
        PreambleLength,                              // LSB PbLengthTX
        PayloadLength,                               // PayloadLen
        static_cast<uint8_t>(packetLengthType << 2 | InvertIQ),
    };
    hal.WriteCommand(LR2021_RADIO_SET_LORA_PACKET_PARAMS_OC, buf, sizeof(buf), radioNumber);
}

void LR2021Driver::ConfigModParamsFSK(const uint32_t Bitrate, const uint8_t BWF, const uint32_t Fdev, const SX12XX_Radio_Number_t radioNumber)
{
    // 8.1.1 SetPacketType
    constexpr uint8_t packetType = LR2021_RADIO_PKT_TYPE_FSK;
    hal.WriteCommand(LR2021_RADIO_SET_PKT_TYPE_OC, &packetType, 1, radioNumber);

    // 11.3.1 SetFskModulationParams
    const uint8_t buf[] {
        static_cast<uint8_t>(Bitrate >> 24),
        static_cast<uint8_t>(Bitrate >> 16),
        static_cast<uint8_t>(Bitrate >> 8),
        static_cast<uint8_t>(Bitrate),
        LR2021_RADIO_GFSK_PULSE_SHAPE_OFF, // Pulse Shape - 0x00: No filter applied
        BWF,
        static_cast<uint8_t>(Fdev >> 16),
        static_cast<uint8_t>(Fdev >> 8),
        static_cast<uint8_t>(Fdev >> 0),
    };
    hal.WriteCommand(LR2021_RADIO_SET_FSK_MODULATION_PARAMS_OC, buf, sizeof(buf), radioNumber);
}

void LR2021Driver::SetPacketParamsFSK(const uint8_t PreambleLength, const SX12XX_Radio_Number_t radioNumber)
{
    // 11.3.2 SetFskPacketParams
    const uint8_t packetParams[] {
        0,                                             // MSB PbLengthTX
        PreambleLength,                                // LSB PbLengthTX
        LR2021_RADIO_GFSK_PREAMBLE_DETECTOR_MIN_8BITS, // Pbl Detect
        0x00,                                          // address filtering disabled / fixed length packet
        0,                                             // MSB PayloadLen
        PayloadLength,                                 // LSB PayloadLen
        LR2021_RADIO_GFSK_CRC_OFF | LR2021_RADIO_GFSK_DC_FREE_WHITENING,
    };
    hal.WriteCommand(LR2021_RADIO_SET_FSK_PACKET_PARAMS_OC, packetParams, sizeof(packetParams), radioNumber);

    // 11.3.3 SetFskWhiteningParams
    constexpr uint16_t lr1121DefaultWhitening = 0x0100;
    constexpr uint8_t whiteningParams[] {
        LR2021_GFSK_WHITENING_TYPE_SX126X_LR11XX | lr1121DefaultWhitening >> 8,
        lr1121DefaultWhitening & 0xFF,
    };
    hal.WriteCommand(LR2021_RADIO_SET_FSK_WHITENING_PARAMS_OC, whiteningParams, sizeof(whiteningParams), radioNumber);
}

void LR2021Driver::SetFSKSyncWord(const uint8_t fskSyncWord1, const uint8_t fskSyncWord2, const SX12XX_Radio_Number_t radioNumber)
{
    // 11.3.5 SetFskSyncWord
    // SyncWordLen is 16 bits.  Fill the rest with preamble bytes.
    const uint8_t syncBuf[] {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, fskSyncWord1, fskSyncWord2, LR2021_GFSK_SYNCWORD_BIT_ORDER_MSB | 16};
    hal.WriteCommand(LR2021_RADIO_SET_FSK_SYNC_WORD_OC, syncBuf, sizeof(syncBuf), radioNumber);
}

void LR2021Driver::SetDioAsRfSwitch()
{
    /*
     * The radio_rfsw_ctrl has 7 values which set the pin (DIO5-11) state for each rf mode.
     * BIT | Mode
     * ----------
     *  4  | 2.4 TX
     *  3  | 2.4 RX
     *  2  | SubG TX
     *  1  | SubG RX
     *  0  | Standby
     * A value of 0xFF marks that DIO as the IRQ pin instead of an RF-switch output.
     *
     * The NiceRF LoRa2021 module has no external RF switch (separate sub-GHz / 2.4G
     * antenna ports, internal band routing) and breaks out DIO7/8/9; the IRQ is wired
     * to the MCU on DIO9.  So DIO9 is the IRQ; the other lines default to RF-switch
     * outputs (harmless when unconnected).  A target may override via LR2021_RFSW_CTRL.
     */
    constexpr uint8_t default_rfsw_ctrl[] {
        0x10,   // DIO5 = 2.4 TX
        0x08,   // DIO6 = 2.4 RX
        0x04,   // DIO7 = subGHz TX
        0x02,   // DIO8 = subGHz RX
        0xFF,   // DIO9 = IRQ pin
        0x00,   // DIO10 = nothing
        0x00,   // DIO11 = nothing
    };

    for (int i = 5; i <= 11; i++)
    {
#if defined(LR2021_RFSW_CTRL)
        const uint8_t pinConfig = LR2021_RFSW_CTRL[i - 5];
#else
        const uint8_t pinConfig = default_rfsw_ctrl[i - 5];
#endif
        uint8_t switchbuf[2];
        switchbuf[0] = i;
        if (pinConfig == 0xFF) {
            // Set DIO as Interrupt pin
            switchbuf[1] = 0x10; // IRQ, SLEEP PullNone
            hal.WriteCommand(LR2021_SYSTEM_SET_DIO_FUNCTION_OC, switchbuf, sizeof(switchbuf), SX12XX_Radio_All);
            SetDioIrqParams(i);
        } else {
            // Set DIO as RF output, and set the RF state(s) for the DIO
            switchbuf[1] = i == 5 ? 0x22 : 0x23; // RF switch on DIO, SLEEP PullAuto (except DIO5 which *must* be PullUp)
            hal.WriteCommand(LR2021_SYSTEM_SET_DIO_FUNCTION_OC, switchbuf, sizeof(switchbuf), SX12XX_Radio_All);
            switchbuf[1] = pinConfig;
            hal.WriteCommand(LR2021_SYSTEM_SET_DIO_RF_SWITCH_CFG_OC, switchbuf, sizeof(switchbuf), SX12XX_Radio_All);
        }
    }
}

void LR2021Driver::CorrectRegisterForSF6(const uint8_t sf, const SX12XX_Radio_Number_t radioNumber)
{
    // SF6 can be made compatible with the SX127x family in implicit mode via a register setting.
    // 3.7.3 WriteRegMemMask32
    if (sf == LR2021_RADIO_LORA_SF6)
    {
        constexpr uint8_t wrbuf[] {
            // Address
            static_cast<uint8_t>(LR20XX_WORKAROUND_LORA_SX1276_COMPATIBILITY_REGISTER_ADDRESS >> 16),
            static_cast<uint8_t>(LR20XX_WORKAROUND_LORA_SX1276_COMPATIBILITY_REGISTER_ADDRESS >> 8),
            static_cast<uint8_t>(LR20XX_WORKAROUND_LORA_SX1276_COMPATIBILITY_REGISTER_ADDRESS >> 0),
            // Mask
            static_cast<uint8_t>(LR20XX_WORKAROUND_LORA_SX1276_COMPATIBILITY_REGISTER_MASK >> 24),
            static_cast<uint8_t>(LR20XX_WORKAROUND_LORA_SX1276_COMPATIBILITY_REGISTER_MASK >> 16),
            static_cast<uint8_t>(LR20XX_WORKAROUND_LORA_SX1276_COMPATIBILITY_REGISTER_MASK >> 8),
            static_cast<uint8_t>(LR20XX_WORKAROUND_LORA_SX1276_COMPATIBILITY_REGISTER_MASK >> 0),
            // Data (bit 19 set)
            0x00, 0x08, 0x00, 0x00,
        };
        hal.WriteCommand(LR2021_REGMEM_WRITE_REGMEM32_MASK_OC, wrbuf, sizeof(wrbuf), radioNumber);
    }
}

/***
 * @brief: Schedule an output power change after the next transmit
 ***/
void LR2021Driver::SetOutputPower(const int8_t power, const bool isSubGHz)
{
    uint8_t pwrNew;

    if (isSubGHz)
    {
        pwrNew = constrain(power, LR2021_POWER_MIN_LF_PA, LR2021_POWER_MAX_LF_PA);

        if ((pwrPendingLF == PWRPENDING_NONE && pwrCurrentLF != pwrNew) || pwrPendingLF != pwrNew)
        {
            pwrPendingLF = pwrNew;
        }
    }
    else
    {
        pwrNew = constrain(power, LR2021_POWER_MIN_HF_PA, LR2021_POWER_MAX_HF_PA);

        if ((pwrPendingHF == PWRPENDING_NONE && pwrCurrentHF != pwrNew) || pwrPendingHF != pwrNew)
        {
            pwrPendingHF = pwrNew;
        }
    }
}

void ICACHE_RAM_ATTR LR2021Driver::CommitOutputPower()
{
    if (pwrPendingLF != PWRPENDING_NONE)
    {
        pwrCurrentLF = pwrPendingLF;
        pwrPendingLF = PWRPENDING_NONE;
        pwrForceUpdate = true;
    }

    if (pwrPendingHF != PWRPENDING_NONE)
    {
        pwrCurrentHF = pwrPendingHF;
        pwrPendingHF = PWRPENDING_NONE;
        pwrForceUpdate = true;
    }

    if (pwrForceUpdate)
    {
        WriteOutputPower(radio1isSubGHz ? pwrCurrentLF : pwrCurrentHF, SX12XX_Radio_1);
        if (GPIO_PIN_NSS_2 != UNDEF_PIN)
        {
            WriteOutputPower(radio2isSubGHz ? pwrCurrentLF : pwrCurrentHF, SX12XX_Radio_2);
        }
        pwrForceUpdate = false;
    }
}

void ICACHE_RAM_ATTR LR2021Driver::WriteOutputPower(const uint8_t power, const SX12XX_Radio_Number_t radioNumber)
{
    const uint8_t Txbuf[] {
        power,
        LR2021_RADIO_RAMP_48_US,
    };

    // 9.5.2 SetTxParams
    hal.WriteCommand(LR2021_RADIO_SET_TX_PARAMS_OC, Txbuf, sizeof(Txbuf), radioNumber);
}

void ICACHE_RAM_ATTR LR2021Driver::SetPaConfig(const bool isSubGHz, const SX12XX_Radio_Number_t radioNumber)
{
    // 7.3.1 SetPaConfig
    if (isSubGHz)
    {
        constexpr uint8_t Pabuf[] {
            LR2021_RADIO_PA_SEL_LF,
            7 << 4 | 6, // PaDutyCycle
            16,         // PaHFDutyCycle (default when not using HF)
        };
        hal.WriteCommand(LR2021_RADIO_SET_PA_CFG_OC, Pabuf, 3, radioNumber);
    }
    else
    {
        constexpr uint8_t Pabuf[] {
            LR2021_RADIO_PA_SEL_HF,
            6 << 4 | 7, // PaDutyCycle | PaSlices (default when not using LF)
            30,         // PaHFDutyCycle
        };
        hal.WriteCommand(LR2021_RADIO_SET_PA_CFG_OC, Pabuf, 3, radioNumber);
    }
}

void LR2021Driver::SetMode(const lr20xx_RadioOperatingModes_t OPmode, const SX12XX_Radio_Number_t radioNumber)
{
    WORD_ALIGNED_ATTR uint8_t buf[5] = {0};

    switch (OPmode)
    {
    case LR2021_MODE_SLEEP:
        // 2.1.5.1 SetSleep
        hal.WriteCommand(LR2021_SYSTEM_SET_SLEEP_OC, buf, 5, radioNumber);
        break;

    case LR2021_MODE_STDBY_RC:
        // 2.1.2.1 SetStandby
        buf[0] = 0x00;
        hal.WriteCommand(LR2021_SYSTEM_SET_STANDBY_OC, buf, 1, radioNumber);
        break;

    case LR2021_MODE_STDBY_XOSC:
        // 2.1.2.1 SetStandby
        buf[0] = 0x01;
        hal.WriteCommand(LR2021_SYSTEM_SET_STANDBY_OC, buf, 1, radioNumber);
        break;

    case LR2021_MODE_FS:
        // 2.1.9.1 SetFs
        hal.WriteCommand(LR2021_SYSTEM_SET_FS_OC, radioNumber);
        break;

    case LR2021_MODE_RX_CONT:
        // 6.3.5 SetRx
        hal.WriteCommand(LR2021_RADIO_SET_RX_OC, radioNumber);
        break;

    case LR2021_MODE_TX:
        // 6.3.6: SetTx
        hal.WriteCommand(LR2021_RADIO_SET_TX_OC, radioNumber);
        break;

    case LR2021_MODE_CAD:
        break;

    default:
        break;
    }
}

void ICACHE_RAM_ATTR LR2021Driver::SetFrequencyReg(const uint32_t freq, const SX12XX_Radio_Number_t radioNumber, const bool doRx, const uint32_t rxTime)
{
    const uint8_t buf[] {
        static_cast<uint8_t>(freq >> 24),
        static_cast<uint8_t>(freq >> 16),
        static_cast<uint8_t>(freq >> 8),
        static_cast<uint8_t>(freq),
    };
    // 7.2.1 SetRfFrequency
    hal.WriteCommand(LR2021_RADIO_SET_RF_FREQUENCY_OC, buf, 4, radioNumber);
    if (doRx)
    {
        SetMode(LR2021_MODE_RX_CONT, radioNumber);
    }
    currFreq = freq;
}

// 4.1.1 SetDioIrqParams
void LR2021Driver::SetDioIrqParams(const uint8_t dio)
{
    constexpr uint32_t enable = LR2021_IRQ_TX_DONE | LR2021_IRQ_RX_DONE;
    const uint8_t buf[] {
        dio,
        static_cast<uint8_t>(enable >> 24), static_cast<uint8_t>(enable >> 16), static_cast<uint8_t>(enable >> 8), static_cast<uint8_t>(enable),
    };
    hal.WriteCommand(LR2021_SYSTEM_SET_DIOIRQPARAMS_OC, buf, sizeof(buf), SX12XX_Radio_All);
}

// 3.4.1 GetStatus / GetAndClearIrqStatus
uint32_t ICACHE_RAM_ATTR LR2021Driver::GetIrqStatus(const SX12XX_Radio_Number_t radioNumber)
{
    uint8_t status[] {
        LR2021_SYSTEM_CLEAR_IRQ_OC >> 8,
        LR2021_SYSTEM_CLEAR_IRQ_OC & 0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
    };
    hal.ReadCommand(status, sizeof(status), radioNumber);
    return status[2] << 24 | status[3] << 16 | status[4] << 8 | status[5];
}

void ICACHE_RAM_ATTR LR2021Driver::ClearIrqStatus(const SX12XX_Radio_Number_t radioNumber)
{
    constexpr uint8_t status[] {
        0xFF,
        0xFF,
        0xFF,
        0xFF,
    };
    hal.WriteCommand(LR2021_SYSTEM_CLEAR_IRQ_OC, status, sizeof(status), radioNumber);
}

void ICACHE_RAM_ATTR LR2021Driver::TXnbISR()
{
#ifdef DEBUG_LR2021_OTA_TIMING
    endTX = micros();
    DBGLN("TOA: %d", endTX - beginTX);
#endif
    CommitOutputPower();
    TXdoneCallback();
}

void ICACHE_RAM_ATTR LR2021Driver::TXnb(uint8_t *data, const bool sendGeminiBuffer, uint8_t *dataGemini, const SX12XX_Radio_Number_t radioNumber)
{
    transmittingRadio = radioNumber;
    if (radioNumber == SX12XX_Radio_NONE)
    {
        SetMode(fallBackMode, SX12XX_Radio_All);
        return;
    }

#if defined(DEBUG_RCVR_SIGNAL_STATS)
    if (radioNumber == SX12XX_Radio_All || radioNumber == SX12XX_Radio_1)
    {
        rxSignalStats[0].telem_count++;
    }
    if (radioNumber == SX12XX_Radio_All || radioNumber == SX12XX_Radio_2)
    {
        rxSignalStats[1].telem_count++;
    }
#endif

    // Normal diversity mode
    if (GPIO_PIN_NSS_2 != UNDEF_PIN && radioNumber != SX12XX_Radio_All)
    {
        // Make sure the unused radio is in FS mode and will not receive the tx packet.
        if (radioNumber == SX12XX_Radio_1)
        {
            SetMode(fallBackMode, SX12XX_Radio_2);
        }
        else
        {
            SetMode(fallBackMode, SX12XX_Radio_1);
        }
    }

    WORD_ALIGNED_ATTR uint8_t outBuffer[32] = {0};
    codec->encode(outBuffer, data, PayloadLength);
    if (sendGeminiBuffer)
    {
        hal.WriteCommand(LR2021_RADIO_WRITE_TX_FIFO, outBuffer, PayloadLength, SX12XX_Radio_1);
        codec->encode(outBuffer, dataGemini, PayloadLength);
        hal.WriteCommand(LR2021_RADIO_WRITE_TX_FIFO, outBuffer, PayloadLength, SX12XX_Radio_2);
    }
    else
    {
        hal.WriteCommand(LR2021_RADIO_WRITE_TX_FIFO, outBuffer, PayloadLength, radioNumber);
    }
    SetMode(LR2021_MODE_TX, radioNumber);
#ifdef DEBUG_LR2021_OTA_TIMING
    beginTX = micros();
#endif
}

void ICACHE_RAM_ATTR LR2021Driver::DecodeRssiSnr(const SX12XX_Radio_Number_t radioNumber)
{
    WORD_ALIGNED_ATTR uint8_t buf[8] {};
    if (useFSK)
    {
        hal.WriteCommand(LR2021_RADIO_GET_FSK_PACKET_STATUS_OC, radioNumber);
        hal.ReadCommand(buf, 8, radioNumber);
    }
    else
    {
        hal.WriteCommand(LR2021_RADIO_GET_LORA_PACKET_STATUS_OC, radioNumber);
        hal.ReadCommand(buf, 8, radioNumber);
    }

    // RssiPkt defines the average RSSI over the last packet received. RSSI value in dBm is -RssiPkt
    const int8_t rssi = -static_cast<int8_t>(buf[useFSK ? 4 : 6]);

    // If radio # is 0, update LastPacketRSSI, otherwise LastPacketRSSI2
    radioNumber == SX12XX_Radio_1 ? LastPacketRSSI = rssi : LastPacketRSSI2 = rssi;

    // Update whatever SNRs we have
    LastPacketSNRRaw = useFSK ? 0 : static_cast<int8_t>(buf[4]);

#if defined(DEBUG_RCVR_SIGNAL_STATS)
    // stat updates
    int i = radioNumber == SX12XX_Radio_1 ? 0 : 1;
    rxSignalStats[i].irq_count++;
    rxSignalStats[i].rssi_sum += rssi;
    rxSignalStats[i].snr_sum += LastPacketSNRRaw;
    if (LastPacketSNRRaw > rxSignalStats[i].snr_max)
    {
        rxSignalStats[i].snr_max = LastPacketSNRRaw;
    }
#endif
}

bool ICACHE_RAM_ATTR LR2021Driver::RXnbISR(const SX12XX_Radio_Number_t radioNumber)
{
    // GetPacket
    memset(rx_buf, 0, sizeof(rx_buf));
    rx_buf[0] = LR2021_RADIO_READ_RX_FIFO >> 8;
    rx_buf[1] = LR2021_RADIO_READ_RX_FIFO;
    hal.ReadCommand(rx_buf, PayloadLength + 2, radioNumber);
    codec->decode(RXdataBuffer, rx_buf + 2, PayloadLength);
    hal.WriteCommand(LR2021_SYSTEM_CLEAR_RX_FIFO_OC, radioNumber);
    if (!RXdoneCallback(SX12XX_RX_OK))
    {
#if defined(DEBUG_RCVR_SIGNAL_STATS)
        rxSignalStats[radioNumber == SX12XX_Radio_1 ? 0 : 1].fail_count++;
#endif
        return false;
    }
    return true;
}

void ICACHE_RAM_ATTR LR2021Driver::RXnb()
{
    SetMode(LR2021_MODE_RX_CONT, SX12XX_Radio_All);
}

bool ICACHE_RAM_ATTR LR2021Driver::GetFrequencyErrorbool(SX12XX_Radio_Number_t radioNumber)
{
    return false;
}

// 7.2.8 GetRssiInst
void ICACHE_RAM_ATTR LR2021Driver::StartRssiInst(const SX12XX_Radio_Number_t radioNumber)
{
    hal.WriteCommand(LR2021_RADIO_GET_RSSI_INST_OC, radioNumber);
}

int8_t ICACHE_RAM_ATTR LR2021Driver::GetRssiInst(const SX12XX_Radio_Number_t radioNumber)
{
    uint8_t status[4] = {0};
    hal.ReadCommand(status, sizeof(status), radioNumber);
    return -static_cast<int8_t>(status[2]); // status[3] contains the bottom bit of 0.5dB so we ignore it
}

void ICACHE_RAM_ATTR LR2021Driver::CheckForSecondPacket()
{
    hasSecondRadioGotData = false;
    if (GPIO_PIN_NSS_2 != UNDEF_PIN)
    {
        constexpr SX12XX_Radio_Number_t radio[2] = {SX12XX_Radio_1, SX12XX_Radio_2};
        const uint8_t processingRadioIdx = (instance->processingPacketRadio == SX12XX_Radio_1) ? 0 : 1;
        const uint8_t secondRadioIdx = !processingRadioIdx;
        const uint32_t secondIrqStatus = instance->GetIrqStatus(radio[secondRadioIdx]);
        if (secondIrqStatus & LR2021_IRQ_RX_DONE)
        {
            memset(rx2_buf, 0, sizeof(rx2_buf));
            rx2_buf[0] = LR2021_RADIO_READ_RX_FIFO >> 8;
            rx2_buf[1] = LR2021_RADIO_READ_RX_FIFO;
            hal.ReadCommand(rx2_buf, PayloadLength + 2, radio[secondRadioIdx]);
            codec->decode(RXdataBufferSecond, rx2_buf + 2, PayloadLength);
            hasSecondRadioGotData = true;
        }
    }
}

void ICACHE_RAM_ATTR LR2021Driver::GetLastPacketStats()
{
    const SX12XX_Radio_Number_t radioNumber = processingPacketRadio == SX12XX_Radio_1 ? SX12XX_Radio_2 : SX12XX_Radio_1;

    // by default, set the strongest receiving radio to be the current processing radio (which got a successful packet)
    strongestReceivingRadio = processingPacketRadio;
    DecodeRssiSnr(processingPacketRadio);
#if defined(DEBUG_RCVR_SIGNAL_STATS)
    irq_count_or++;
#endif

    if (GPIO_PIN_NSS_2 != UNDEF_PIN)
    {
        // when both radio got the packet, use the better RSSI one
        if (hasSecondRadioGotData)
        {
            const int8_t firstSNR = LastPacketSNRRaw;
            DecodeRssiSnr(radioNumber);
            LastPacketSNRRaw = fuzzy_snr(LastPacketSNRRaw, firstSNR, FuzzySNRThreshold);
            // Update the strongest receiving radio to be the one with better signal strength
            strongestReceivingRadio = LastPacketRSSI > LastPacketRSSI2 ? SX12XX_Radio_1 : SX12XX_Radio_2;
#if defined(DEBUG_RCVR_SIGNAL_STATS)
            irq_count_both++;
        }
        else
        {
            rxSignalStats[radioNumber == SX12XX_Radio_1 ? 0 : 1].fail_count++;
#endif
        }
    }
}

void ICACHE_RAM_ATTR LR2021Driver::IsrCallback_1()
{
    IsrCallback(SX12XX_Radio_1);
}

void ICACHE_RAM_ATTR LR2021Driver::IsrCallback_2()
{
    IsrCallback(SX12XX_Radio_2);
}

void ICACHE_RAM_ATTR LR2021Driver::IsrCallback(const SX12XX_Radio_Number_t radioNumber)
{
    instance->processingPacketRadio = radioNumber;
    const SX12XX_Radio_Number_t otherRadioNumber = radioNumber == SX12XX_Radio_1 ? SX12XX_Radio_2 : SX12XX_Radio_1;

    const uint32_t irqStatus = instance->GetIrqStatus(radioNumber);
    if (irqStatus & LR2021_IRQ_TX_DONE)
    {
        instance->TXnbISR();
        if (GPIO_PIN_NSS_2 != UNDEF_PIN)
        {
            instance->ClearIrqStatus(otherRadioNumber);
        }
    }
    else if (irqStatus & LR2021_IRQ_RX_DONE)
    {
        instance->RXnbISR(radioNumber);
    }
}

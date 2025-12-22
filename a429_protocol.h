#ifndef A429_PROTOCOL_H
#define A429_PROTOCOL_H

#include <cstdint>
#include <cstring>

// Constants defined in System Constraints
constexpr int MAX_TX_CHANNELS = 5;
constexpr int MAX_RX_CHANNELS = 10;
constexpr int DATA_FIELD_COUNT = 10; // Data[0..9]
constexpr uint32_t DEFAULT_UNUSED_FIELD = 0xFFFFFFFF;

// Message Types (Section 3)
enum class MsgType : uint32_t {
    NONE            = 0,
    TX              = 1, // Transfer message (to HW)
    RX              = 2, // Receive message (from HW)
    TX_STS          = 3, // TX channel status
    RX_STS          = 4, // RX channel status
    TX_CFG          = 5, // TX channel configuration
    RX_CFG          = 6, // RX channel configuration
    TX_CFG_STATUS   = 7, // TX configuration status
    RX_CFG_STATUS   = 8, // RX configuration status
    RESERVED_1      = 9,
    RESERVED_2      = 10
};

// UDP Message Structure
// Using pragma pack to ensure no padding bytes are inserted by the compiler
#pragma pack(push, 1)
struct A429Message {
    uint32_t messageId;      // 0-3
    uint32_t counter;        // 4-7
    MsgType  type;           // 8-11
    uint32_t data[DATA_FIELD_COUNT]; // 12-51

    // Constructor to initialize defaults
    A429Message() : messageId(0), counter(0), type(MsgType::NONE) {
        for (int i = 0; i < DATA_FIELD_COUNT; ++i) {
            data[i] = DEFAULT_UNUSED_FIELD;
        }
    }
};
#pragma pack(pop)

// Unified A429 Word Structure (Data, Config, Status)
union A429Word {
    uint32_t raw;
    struct {
        uint32_t label : 8;
        uint32_t sdi : 2;
        uint32_t data : 19;
        uint32_t ssm : 2;
        uint32_t parity : 1;
    } a429;
    struct {
        uint32_t speed : 1; // 0: Low, 1: High
        uint32_t reserved : 31;
    } config;
    struct {
        uint32_t configured : 1;
        uint32_t reserved : 31;
    } status;

    A429Word() : raw(0) {}
    A429Word(uint32_t r) : raw(r) {}
};

class A429WordHelper {
public:
    // TX / RX – A429 Data Word Construction
    static uint32_t pack(uint8_t label, uint8_t sdi, uint32_t data, uint8_t ssm, bool parity) {
        A429Word word;
        word.a429.label = label;
        word.a429.sdi = sdi;
        word.a429.data = data;
        word.a429.ssm = ssm;
        word.a429.parity = parity;
        return word.raw;
    }

    static void unpack(uint32_t raw_word, uint8_t &label, uint8_t &sdi, uint32_t &data, uint8_t &ssm, bool &parity) {
        A429Word word(raw_word);
        label  = word.a429.label;
        sdi    = word.a429.sdi;
        data   = word.a429.data;
        ssm    = word.a429.ssm;
        parity = word.a429.parity;
    }
};

#endif

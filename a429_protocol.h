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

// Configuration Bit Definitions
enum class ChannelSpeed : uint32_t {
    LOW_SPEED = 0,
    HIGH_SPEED = 1
};

// Status Bit Definitions
constexpr uint32_t STATUS_ALIVE_MASK = 0x00000001;
constexpr uint32_t STATUS_CONFIGURED_MASK = 0x00000001;

class A429WordHelper {
public:
    // TX / RX – A429 Data Word Construction
    static uint32_t pack(uint8_t label, uint8_t sdi, uint32_t data, uint8_t ssm, bool parity) {
        uint32_t word = 0;
        
        // Label: Bits 8-0
        word |= (static_cast<uint32_t>(label) & 0xFF); 
        
        // SDI: Bits 9-8
        word |= (static_cast<uint32_t>(sdi) & 0x03) << 8;
        
        // Data: Bits 28-10 (19 bits)
        word |= (data & 0x7FFFF) << 10;
        
        // SSM: Bits 30-29
        word |= (static_cast<uint32_t>(ssm) & 0x03) << 29;
        
        // Parity: Bit 31
        if (parity) {
            word |= (1U << 31);
        }
        
        return word;
    }

    static void unpack(uint32_t word, uint8_t &label, uint8_t &sdi, uint32_t &data, uint8_t &ssm, bool &parity) {
        label  = static_cast<uint8_t>(word & 0xFF);
        
        sdi    = static_cast<uint8_t>((word >> 8) & 0x03);
        data   = (word >> 10) & 0x7FFFF;
        ssm    = static_cast<uint8_t>((word >> 29) & 0x03);
        parity = (word >> 31) & 0x01;
    }
};

#endif

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <memory>
#include "a429_communicator.h"
#include "a429_protocol.h"
#include "a429_udp.h"

// --- 1. A429Word and Helper Tests ---

// Test the union directly to ensure bitfields are correct
TEST(A429WordUnionTest, BitfieldAccess) {
    A429Word word;
    word.raw = 0; // Start clean

    word.a429.label = 0xAB; // 171
    word.a429.sdi = 0b10;
    word.a429.data = 0x7FFFF; // Max 19-bit value
    word.a429.ssm = 0b01;
    word.a429.parity = 1;

    EXPECT_EQ(word.a429.label, 0xAB);
    EXPECT_EQ(word.a429.sdi, 0b10);
    EXPECT_EQ(word.a429.data, 0x7FFFF);
    EXPECT_EQ(word.a429.ssm, 0b01);
    EXPECT_EQ(word.a429.parity, 1);

    // Test config view
    word.raw = 0;
    word.config.speed = 1;
    EXPECT_EQ(word.raw, 1);

    // Test status view
    word.raw = 0;
    word.status.configured = 1;
    EXPECT_EQ(word.raw, 1);
}

TEST(A429WordHelperTest, PackAndUnpackCorrectly) {
    uint8_t label = 0x12;    // 8-bit
    uint8_t sdi = 0x02;      // 2-bit
    uint32_t data = 0x12345; // 19-bit data
    uint8_t ssm = 0x03;      // 2-bit
    bool parity = true;      // 1-bit

    // Pack
    uint32_t packed = A429WordHelper::pack(label, sdi, data, ssm, parity);

    // Unpack
    uint8_t u_label, u_sdi, u_ssm;
    uint32_t u_data;
    bool u_parity;
    A429WordHelper::unpack(packed, u_label, u_sdi, u_data, u_ssm, u_parity);

    // Verify
    EXPECT_EQ(label, u_label);
    EXPECT_EQ(sdi, u_sdi);
    EXPECT_EQ(data, u_data);
    EXPECT_EQ(ssm, u_ssm);
    EXPECT_EQ(parity, u_parity);
}

// --- 2. A429Communicator Logic Tests (State Machine & Timers) ---

class CommunicatorLogicTest : public ::testing::Test{
protected:
    std::vector<A429Message> sentMessages;
    bool reportCalled = false;
    std::unique_ptr<A429Communicator> comm;

    void SetUp() override {
        sentMessages.clear();
        reportCalled = false;
        // Mock callbacks
        comm = std::make_unique<A429Communicator>(
            [this](const A429Message& msg) { sentMessages.push_back(msg); },
            [this](){ reportCalled = true; }
        );
    }

    // Helper function to configure specific channels
    void ConfigureChannels(const std::vector<int>& txChannels, const std::vector<int>& rxChannels, std::chrono::steady_clock::time_point now) {
        A429Word statusWord;
        statusWord.status.configured = 1;

        if (!txChannels.empty()) {
            A429Message txStatus;
            txStatus.type = MsgType::TX_CFG_STATUS;
            for(int i=0; i<MAX_TX_CHANNELS; ++i) txStatus.data[i] = 0;
            for(int idx : txChannels) txStatus.data[idx] = statusWord.raw;
            comm->onPacketReceived(txStatus, now);
        }
        
        if (!rxChannels.empty()) {
            A429Message rxStatus;
            rxStatus.type = MsgType::RX_CFG_STATUS;
            for(int i=0; i<MAX_RX_CHANNELS; ++i) rxStatus.data[i] = 0;
            for(int idx : rxChannels) rxStatus.data[idx] = statusWord.raw;
            comm->onPacketReceived(rxStatus, now);
        }
    }

    // Helper function to reach ERROR_RECOVERY state for specific channel
    void ReachChannelErrorRecovery(int channelIndex, bool isTx) {
        auto now = std::chrono::steady_clock::now();
        comm->update(now);
        
        // Configure the channel
        if (isTx) {
            ConfigureChannels({channelIndex}, {}, now);
        } else {
            ConfigureChannels({}, {channelIndex}, now);
        }
        
        comm->update(now); // Process buffer -> Channel OPERATIONAL

        // Timeout -> Channel ERROR_RECOVERY (no status received for 4 seconds)
        comm->update(now + std::chrono::seconds(5));
    }
};

TEST_F(CommunicatorLogicTest, StartsInStartupAndSendsConfig) {
    EXPECT_EQ(comm->getCurrentState(), CommState::STARTUP);
    
    // First update call should send configuration
    comm->update();
    
    EXPECT_EQ(comm->getCurrentState(), CommState::CONFIGURING);
    EXPECT_GE(sentMessages.size(), 2); // TX_CFG ve RX_CFG
    EXPECT_EQ(sentMessages[0].type, MsgType::TX_CFG);
}

TEST_F(CommunicatorLogicTest, RetransmitsConfigOnTimeout) {
    auto start = std::chrono::steady_clock::now();
    comm->update(start); // First transmission - all channels go to CONFIGURING
    sentMessages.clear();

    // After 3 seconds (No timeout)
    comm->update(start + std::chrono::seconds(3));
    EXPECT_EQ(sentMessages.size(), 0);

    // After 4 seconds (Timeout occurred, should retransmit for unconfigured channels)
    comm->update(start + std::chrono::seconds(5));
    // Each unconfigured channel will send individual config messages
    EXPECT_GE(sentMessages.size(), 1);
}

TEST_F(CommunicatorLogicTest, TransitionsToOperationalWhenConfigured) {
    auto now = std::chrono::steady_clock::now();
    comm->update(now); // Switch to CONFIGURING mode

    // TX Config Status Received Successfully
    A429Word statusWord;
    statusWord.status.configured = 1;

    A429Message txStatus;
    txStatus.type = MsgType::TX_CFG_STATUS;
    for(int i=0; i<MAX_TX_CHANNELS; ++i) txStatus.data[i] = statusWord.raw;
    comm->onPacketReceived(txStatus, now);

    // RX Config Status Received Successfully
    A429Message rxStatus;
    rxStatus.type = MsgType::RX_CFG_STATUS;
    for(int i=0; i<MAX_RX_CHANNELS; ++i) rxStatus.data[i] = statusWord.raw;
    comm->onPacketReceived(rxStatus, now);

    // Process buffered messages
    comm->update(now);

    // Should be OPERATIONAL now
    EXPECT_EQ(comm->getCurrentState(), CommState::OPERATIONAL);
}

TEST_F(CommunicatorLogicTest, ChannelOperationalTimeoutTriggersRecovery) {
    auto now = std::chrono::steady_clock::now();
    comm->update(now);
    
    // Configure TX channel 0
    ConfigureChannels({0}, {}, now);
    comm->update(now);
    
    // Verify channel is operational
    EXPECT_EQ(comm->getTxChannelInfo(0).state, ChannelState::OPERATIONAL);
    EXPECT_TRUE(comm->getTxChannelInfo(0).configured);
    
    // After 5 seconds without status, channel should go to ERROR_RECOVERY
    comm->update(now + std::chrono::seconds(5));
    EXPECT_EQ(comm->getTxChannelInfo(0).state, ChannelState::ERROR_RECOVERY);
    EXPECT_FALSE(comm->getTxChannelInfo(0).configured);
}

TEST_F(CommunicatorLogicTest, TransitionsToOperationalWhenOnlyOneChannelIsConfigured) {
    auto now = std::chrono::steady_clock::now();
    comm->update(now); // Switch to CONFIGURING mode
    EXPECT_EQ(comm->getCurrentState(), CommState::CONFIGURING);

    // Only one TX channel reports status
    A429Word statusWord;
    statusWord.status.configured = 1;
    A429Message txStatus;
    txStatus.type = MsgType::TX_CFG_STATUS;
    // Set all to not-configured first
    for(int i=0; i<MAX_TX_CHANNELS; ++i) txStatus.data[i] = 0;
    // Configure just one
    txStatus.data[2] = statusWord.raw;
    comm->onPacketReceived(txStatus, now);

    // Process buffer
    comm->update(now);

    // Should be OPERATIONAL now
    EXPECT_EQ(comm->getCurrentState(), CommState::OPERATIONAL);
}

TEST_F(CommunicatorLogicTest, TriggersOmdReportOnTimer) {
    // Force transition to OPERATIONAL
    auto now = std::chrono::steady_clock::now();
    comm->update(now); // 1. Go from STARTUP to CONFIGURING
    EXPECT_EQ(comm->getCurrentState(), CommState::CONFIGURING);

    A429Word statusWord;
    statusWord.status.configured = 1;
    A429Message status;
    status.type = MsgType::TX_CFG_STATUS;
    for(int i=0; i<MAX_TX_CHANNELS; ++i) status.data[i] = statusWord.raw;
    comm->onPacketReceived(status, now); // 2. Queue the status packet

    comm->update(now); // 3. Process packet, go from CONFIGURING to OPERATIONAL
    EXPECT_EQ(comm->getCurrentState(), CommState::OPERATIONAL);
    EXPECT_FALSE(reportCalled);

    // Keep connection alive at 3s to prevent 4s timeout
    comm->onPacketReceived(status, now + std::chrono::seconds(3));
    comm->update(now + std::chrono::seconds(3));
    EXPECT_FALSE(reportCalled);

    // After 5 seconds, report should be called
    comm->update(now + std::chrono::seconds(5));
    EXPECT_TRUE(reportCalled);
}

TEST_F(CommunicatorLogicTest, ChannelErrorRecoveryCycle) {
    auto now = std::chrono::steady_clock::now();
    comm->update(now);
    
    // Configure TX channel 2
    ConfigureChannels({2}, {}, now);
    comm->update(now);
    
    EXPECT_EQ(comm->getTxChannelInfo(2).state, ChannelState::OPERATIONAL);
    sentMessages.clear();

    // Timeout -> ERROR_RECOVERY
    comm->update(now + std::chrono::seconds(5));
    EXPECT_EQ(comm->getTxChannelInfo(2).state, ChannelState::ERROR_RECOVERY);
    EXPECT_FALSE(comm->getTxChannelInfo(2).configured);

    // Next update: ERROR_RECOVERY -> IDLE -> CONFIGURING (auto retry)
    comm->update(now + std::chrono::seconds(6));
    EXPECT_GE(sentMessages.size(), 1); // Channel should be reconfigured
}

TEST_F(CommunicatorLogicTest, IndividualChannelStateManagement) {
    auto now = std::chrono::steady_clock::now();
    comm->update(now); // STARTUP -> CONFIGURING
    
    // Initially all channels should be in CONFIGURING state
    for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
        EXPECT_EQ(comm->getTxChannelInfo(i).state, ChannelState::CONFIGURING);
    }
    for (int i = 0; i < MAX_RX_CHANNELS; ++i) {
        EXPECT_EQ(comm->getRxChannelInfo(i).state, ChannelState::CONFIGURING);
    }
    
    // Configure only TX channels 0, 2 and RX channel 5
    ConfigureChannels({0, 2}, {5}, now);
    comm->update(now);
    
    // Check configured channels are OPERATIONAL
    EXPECT_EQ(comm->getTxChannelInfo(0).state, ChannelState::OPERATIONAL);
    EXPECT_TRUE(comm->getTxChannelInfo(0).configured);
    EXPECT_EQ(comm->getTxChannelInfo(2).state, ChannelState::OPERATIONAL);
    EXPECT_TRUE(comm->getTxChannelInfo(2).configured);
    EXPECT_EQ(comm->getRxChannelInfo(5).state, ChannelState::OPERATIONAL);
    EXPECT_TRUE(comm->getRxChannelInfo(5).configured);
    
    // Check unconfigured channels are still CONFIGURING
    EXPECT_EQ(comm->getTxChannelInfo(1).state, ChannelState::CONFIGURING);
    EXPECT_FALSE(comm->getTxChannelInfo(1).configured);
    EXPECT_EQ(comm->getTxChannelInfo(3).state, ChannelState::CONFIGURING);
    EXPECT_FALSE(comm->getTxChannelInfo(3).configured);
}

TEST_F(CommunicatorLogicTest, MultipleChannelTimeouts) {
    auto now = std::chrono::steady_clock::now();
    comm->update(now);
    
    // Configure TX channels 0 and 1
    ConfigureChannels({0, 1}, {}, now);
    comm->update(now);
    
    EXPECT_EQ(comm->getTxChannelInfo(0).state, ChannelState::OPERATIONAL);
    EXPECT_EQ(comm->getTxChannelInfo(1).state, ChannelState::OPERATIONAL);
    
    // Keep only channel 0 alive by sending status
    A429Word statusWord;
    statusWord.status.configured = 1;
    A429Message txStatus;
    txStatus.type = MsgType::TX_CFG_STATUS;
    for(int i=0; i<MAX_TX_CHANNELS; ++i) txStatus.data[i] = 0;
    txStatus.data[0] = statusWord.raw; // Only channel 0 gets status
    
    // At 3 seconds, send status for channel 0 only
    comm->onPacketReceived(txStatus, now + std::chrono::seconds(3));
    comm->update(now + std::chrono::seconds(3));
    
    // Channel 0 should still be OPERATIONAL, channel 1 should timeout
    comm->update(now + std::chrono::seconds(5));
    EXPECT_EQ(comm->getTxChannelInfo(0).state, ChannelState::OPERATIONAL);
    EXPECT_EQ(comm->getTxChannelInfo(1).state, ChannelState::ERROR_RECOVERY);
}

TEST_F(CommunicatorLogicTest, PartialConfigurationRetry) {
    auto start = std::chrono::steady_clock::now();
    comm->update(start); // All channels CONFIGURING
    
    // Configure only TX channel 0
    ConfigureChannels({0}, {}, start);
    comm->update(start);
    
    EXPECT_EQ(comm->getTxChannelInfo(0).state, ChannelState::OPERATIONAL);
    EXPECT_EQ(comm->getTxChannelInfo(1).state, ChannelState::CONFIGURING);
    
    sentMessages.clear();
    
    // After 5 seconds, unconfigured channels should retry
    comm->update(start + std::chrono::seconds(5));
    
    // Channel 1 should have retry config sent
    EXPECT_GE(sentMessages.size(), 1);
    
    // Channel 0 should still be operational
    EXPECT_EQ(comm->getTxChannelInfo(0).state, ChannelState::OPERATIONAL);
}

TEST_F(CommunicatorLogicTest, SystemOperationalWithPartialChannels) {
    auto now = std::chrono::steady_clock::now();
    comm->update(now);
    EXPECT_EQ(comm->getCurrentState(), CommState::CONFIGURING);
    
    // Configure only one TX channel and one RX channel
    ConfigureChannels({1}, {3}, now);
    comm->update(now);
    
    // System should go to OPERATIONAL even with partial configuration
    EXPECT_EQ(comm->getCurrentState(), CommState::OPERATIONAL);
    EXPECT_EQ(comm->getTxChannelInfo(1).state, ChannelState::OPERATIONAL);
    EXPECT_EQ(comm->getRxChannelInfo(3).state, ChannelState::OPERATIONAL);
    
    // Other channels should still be CONFIGURING
    EXPECT_EQ(comm->getTxChannelInfo(0).state, ChannelState::CONFIGURING);
    EXPECT_EQ(comm->getRxChannelInfo(0).state, ChannelState::CONFIGURING);
}

// --- 3. UDP Driver Integration Test ---

class UdpDriverIntegrationTest : public ::testing::Test {
protected:
    const int port1 = 9998;
    const int port2 = 9999;
    const std::string localhost = "127.0.0.1";

    std::unique_ptr<A429UdpDriver> driver1;
    std::unique_ptr<A429UdpDriver> driver2;

    void SetUp() override {
        // Driver 1 listens on port1, sends to port2
        driver1 = std::make_unique<A429UdpDriver>(port1, localhost, port2);
        // Driver 2 listens on port2, sends to port1
        driver2 = std::make_unique<A429UdpDriver>(port2, localhost, port1);
        // Give sockets a moment to bind
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

TEST_F(UdpDriverIntegrationTest, CanSendAndReceive) {
    // 1. driver1 sends a message
    A429Message sentMsg;
    sentMsg.type = MsgType::TX_CFG;
    sentMsg.counter = 123;
    
    bool sent_ok = driver1->send(sentMsg);
    ASSERT_TRUE(sent_ok);

    // 2. driver2 should receive it
    A429Message receivedMsg;
    std::string senderIp;
    
    // Loop briefly to allow packet travel time
    bool received_ok = false;
    for (int i = 0; i < 5; ++i) {
        if (driver2->receive(receivedMsg, senderIp)) {
            received_ok = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    ASSERT_TRUE(received_ok) << "Driver 2 did not receive the packet.";
    EXPECT_EQ(receivedMsg.type, sentMsg.type);
    EXPECT_EQ(receivedMsg.counter, sentMsg.counter);
    EXPECT_EQ(senderIp, localhost);
}

// --- 4. Full Integration Test (Communicator + UDP Driver) ---

TEST(FullIntegrationTest, CommunicatorSendsConfigViaUdpDriver) {
    const int appPort = 8888;
    const int remotePort = 8889;
    const std::string localhost = "127.0.0.1";

    // The "Remote Hardware" that listens for our app's messages
    A429UdpDriver remoteHardware(remotePort, localhost, appPort);

    // Our Application's UDP driver
    A429UdpDriver appDriver(appPort, localhost, remotePort);

    // Our application's communicator, wired to the app's UDP driver
    A429Communicator comm(
        [&appDriver](const A429Message& msg) {
            appDriver.send(msg);
        },
        [] () {}
    );

    // Action: Trigger the communicator's first update
    comm.update();

    // Verification: Check if the remote hardware received the config message
    A429Message receivedMsg;
    std::string senderIp;
    bool received_ok = false;
    for (int i = 0; i < 5; ++i) {
        if (remoteHardware.receive(receivedMsg, senderIp)) {
            received_ok = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(received_ok) << "Remote hardware did not receive the configuration packet.";
    EXPECT_EQ(receivedMsg.type, MsgType::TX_CFG);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
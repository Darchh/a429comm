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

    // Helper function to reach ERROR_RECOVERY state
    void ReachErrorRecoveryState() {
        auto now = std::chrono::steady_clock::now();
        comm->update(now);
        
        A429Word statusWord;
        statusWord.status.configured = 1;

        A429Message status;
        status.type = MsgType::TX_CFG_STATUS;
        for(int i=0; i<MAX_TX_CHANNELS; ++i) status.data[i] = statusWord.raw;
        comm->onPacketReceived(status, now);
        status.type = MsgType::RX_CFG_STATUS;
        for(int i=0; i<MAX_RX_CHANNELS; ++i) status.data[i] = statusWord.raw;
        comm->onPacketReceived(status, now);
        
        comm->update(now); // Process buffer -> OPERATIONAL

        // Timeout -> ERROR_RECOVERY
        comm->update(now + std::chrono::seconds(4));
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
    comm->update(start); // First transmission
    sentMessages.clear();

    // After 3 seconds (No timeout)
    comm->update(start + std::chrono::seconds(3));
    EXPECT_EQ(sentMessages.size(), 0);

    // After 4 seconds (Timeout occurred, should retransmit)
    comm->update(start + std::chrono::seconds(4));
    EXPECT_GE(sentMessages.size(), 2);
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

TEST_F(CommunicatorLogicTest, OperationalTimeoutTriggersRecovery) {
    ReachErrorRecoveryState();
    EXPECT_EQ(comm->getCurrentState(), CommState::ERROR_RECOVERY);
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

TEST_F(CommunicatorLogicTest, ErrorRecoveryCycle) {
    // Go to OPERATIONAL and then trigger ERROR_RECOVERY
    ReachErrorRecoveryState();
    EXPECT_EQ(comm->getCurrentState(), CommState::ERROR_RECOVERY);
    sentMessages.clear();

    // First update: ERROR_RECOVERY -> STARTUP
    comm->update();
    EXPECT_EQ(comm->getCurrentState(), CommState::STARTUP);

    // Second update: STARTUP -> CONFIGURING (and sends config)
    comm->update();
    EXPECT_EQ(comm->getCurrentState(), CommState::CONFIGURING);
    EXPECT_GE(sentMessages.size(), 2); // Check if config was re-sent
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
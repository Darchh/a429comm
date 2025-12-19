#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include "a429_communicator.h"
#include "a429_protocol.h"

// --- Helper Class for UDP Testing ---
// This class simulates hardware and captures sent UDP packets.
class UdpReceiverMock {
    int sock;
    struct sockaddr_in addr;
public:
    UdpReceiverMock(int port) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        
        if (bind(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
            throw std::runtime_error("Bind failed in test helper");
        }
        
        // 1 second timeout so the test doesn't wait forever
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    }
    
    ~UdpReceiverMock() {
        close(sock);
    }
    
    bool receive(A429Message& msg) {
        ssize_t len = recv(sock, &msg, sizeof(msg), 0);
        return len == sizeof(msg);
    }
};

// --- 1. A429WordHelper Unit Tests (Bit Manipulation) ---

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

class CommunicatorLogicTest : public ::testing::Test {
protected:
    std::vector<A429Message> sentMessages;
    bool reportCalled = false;
    A429Communicator* comm;

    void SetUp() override {
        sentMessages.clear();
        reportCalled = false;
        // Mock callbacks
        comm = new A429Communicator(
            [this](const A429Message& msg) { sentMessages.push_back(msg); },
            [this]() { reportCalled = true; }
        );
    }

    void TearDown() override {
        delete comm;
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
    A429Message txStatus;
    txStatus.type = MsgType::TX_CFG_STATUS;
    for(int i=0; i<MAX_TX_CHANNELS; ++i) txStatus.data[i] = STATUS_CONFIGURED_MASK;
    comm->onPacketReceived(txStatus, now);

    // RX Config Status Received Successfully
    A429Message rxStatus;
    rxStatus.type = MsgType::RX_CFG_STATUS;
    for(int i=0; i<MAX_RX_CHANNELS; ++i) rxStatus.data[i] = STATUS_CONFIGURED_MASK;
    comm->onPacketReceived(rxStatus, now);

    // Should be OPERATIONAL now
    EXPECT_EQ(comm->getCurrentState(), CommState::OPERATIONAL);
}

TEST_F(CommunicatorLogicTest, OperationalTimeoutTriggersRecovery) {
    // Force transition to OPERATIONAL mode
    auto now = std::chrono::steady_clock::now();
    comm->update(now);
    // (Normally we would transition via status messages, but assume for brevity)
    // Simulate normal flow since we cannot access private members for testing:
    A429Message status;
    status.type = MsgType::TX_CFG_STATUS; 
    for(int i=0; i<MAX_TX_CHANNELS; ++i) status.data[i] = STATUS_CONFIGURED_MASK;
    comm->onPacketReceived(status, now);
    status.type = MsgType::RX_CFG_STATUS;
    for(int i=0; i<MAX_RX_CHANNELS; ++i) status.data[i] = STATUS_CONFIGURED_MASK;
    comm->onPacketReceived(status, now);
    
    EXPECT_EQ(comm->getCurrentState(), CommState::OPERATIONAL);

    // If no message received for 4 seconds -> ERROR_RECOVERY
    comm->update(now + std::chrono::seconds(4));
    EXPECT_EQ(comm->getCurrentState(), CommState::ERROR_RECOVERY);
}

// --- 3. UDP Integration Test (Real Socket) ---

TEST(UdpIntegrationTest, SendsDataOverRealSocket) {
    int testPort = 9999;
    UdpReceiverMock receiver(testPort); // Listener (Hardware simulation)
    
    // Sender Socket (Software)
    int senderSock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(testPort);
    destAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    A429Communicator comm(
        [senderSock, destAddr](const A429Message& msg) {
            sendto(senderSock, &msg, sizeof(msg), 0, (const struct sockaddr*)&destAddr, sizeof(destAddr));
        },
        [](){}
    );

    comm.update(); // Sends message

    A429Message receivedMsg;
    bool success = receiver.receive(receivedMsg);
    
    EXPECT_TRUE(success) << "UDP packet did not reach the other side!";
    EXPECT_EQ(receivedMsg.type, MsgType::TX_CFG); // We expect the first message to be TX configuration

    close(senderSock);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
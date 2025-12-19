#ifndef A429_COMMUNICATOR_H
#define A429_COMMUNICATOR_H

#include <iostream>
#include <chrono>
#include <functional>
#include "a429_protocol.h"

enum class CommState {
    STARTUP,
    CONFIGURING,
    OPERATIONAL,
    ERROR_RECOVERY
};

class A429Communicator {
public:
    using SendCallback = std::function<void(const A429Message&)>;
    using ReportCallback = std::function<void()>;

private:
    CommState currentState;
    std::chrono::steady_clock::time_point lastConfigStatusReceived;
    std::chrono::steady_clock::time_point lastConfigSent;
    std::chrono::steady_clock::time_point lastOmdReport;
    
    uint32_t txCounter = 0;
    bool txConfigured[MAX_TX_CHANNELS];
    bool rxConfigured[MAX_RX_CHANNELS];

    SendCallback sendCallback;
    ReportCallback reportCallback;

public:
    A429Communicator(SendCallback sendCb, ReportCallback reportCb) 
        : currentState(CommState::STARTUP), sendCallback(sendCb), reportCallback(reportCb) {
        resetConfigurationStatus();
    }

    void resetConfigurationStatus() {
        for(bool &b : txConfigured) b = false;
        for(bool &b : rxConfigured) b = false;
    }

    void update(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        switch (currentState) {
            case CommState::STARTUP:
                sendConfiguration();
                currentState = CommState::CONFIGURING;
                lastConfigSent = now;
                break;

            case CommState::CONFIGURING:
                // Timeout Rule: If no response within 4 seconds, retransmit
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastConfigSent).count() >= 4) {
                    std::cerr << "[Error] Configuration Timeout. Retrying..." << std::endl;
                    sendConfiguration(); // Resend for unconfigured channels
                    lastConfigSent = now;
                }
                break;

            case CommState::OPERATIONAL:
                // Operational Rule: Expect CONFIG STATUS every 1s. 
                // Timeout Rule: If not received for 4s, return to configuration
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastConfigStatusReceived).count() >= 4) {
                    std::cerr << "[Error] Connection Lost (No CONFIG STATUS). Reverting to Configuration." << std::endl;
                    currentState = CommState::ERROR_RECOVERY;
                }
                
                // Operational Rule: TX/RX STATUS transmitted every 5s to OMD
                // (Assuming OMD reporting is internal to simulator logic)
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastOmdReport).count() >= 5) {
                    if (reportCallback) reportCallback();
                    lastOmdReport = now;
                }
                break;

            case CommState::ERROR_RECOVERY:
                // Reset state and attempt reconfiguration
                resetConfigurationStatus();
                currentState = CommState::STARTUP;
                break;
        }
    }

    // Simulates receiving a UDP packet
    void onPacketReceived(const A429Message& msg, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        if (msg.type == MsgType::TX_CFG_STATUS) {
            lastConfigStatusReceived = now;
            for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
                if (msg.data[i] & STATUS_CONFIGURED_MASK) {
                    txConfigured[i] = true;
                }
            }
            checkConfigurationComplete(now);
        }
        else if (msg.type == MsgType::RX_CFG_STATUS) {
            lastConfigStatusReceived = now;
            for (int i = 0; i < MAX_RX_CHANNELS; ++i) {
                if (msg.data[i] & STATUS_CONFIGURED_MASK) {
                    rxConfigured[i] = true;
                }
            }
            checkConfigurationComplete(now);
        }
        else if (currentState == CommState::OPERATIONAL) {
            // Handle standard RX/TX data
        }
    }

    // Getter for testing purposes
    CommState getCurrentState() const {
        return currentState;
    }

private:
    void checkConfigurationComplete(std::chrono::steady_clock::time_point now) {
        if (currentState != CommState::CONFIGURING) return;

        bool allTx = true;
        for (bool b : txConfigured) if (!b) allTx = false;

        bool allRx = true;
        for (bool b : rxConfigured) if (!b) allRx = false;

        if (allTx && allRx) {
            std::cout << "All channels configured. Entering OPERATIONAL state." << std::endl;
            currentState = CommState::OPERATIONAL;
            // Reset timers to prevent immediate timeout
            lastConfigStatusReceived = now;
            lastOmdReport = now;
        }
    }

    void sendConfiguration() {
        std::cout << "Sending Configuration Message..." << std::endl;

        // Send TX Configuration
        A429Message txMsg;
        txMsg.type = MsgType::TX_CFG;
        bool sendTx = false;
        for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
            if (!txConfigured[i]) {
                txMsg.data[i] = static_cast<uint32_t>(ChannelSpeed::HIGH_SPEED); // Default to High Speed
                sendTx = true;
            }
        }
        if (sendTx) sendToHardware(txMsg);

        // Send RX Configuration
        A429Message rxMsg;
        rxMsg.type = MsgType::RX_CFG;
        bool sendRx = false;
        for (int i = 0; i < MAX_RX_CHANNELS; ++i) {
            if (!rxConfigured[i]) {
                rxMsg.data[i] = static_cast<uint32_t>(ChannelSpeed::LOW_SPEED); // Default to Low Speed
                sendRx = true;
            }
        }
        if (sendRx) sendToHardware(rxMsg);
    }

    void sendToHardware(A429Message& msg) {
        msg.counter = txCounter++;
        if (sendCallback) sendCallback(msg);
    }
};

#endif
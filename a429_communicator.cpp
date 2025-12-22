#include "a429_communicator.h"

A429Communicator::A429Communicator(SendCallback sendCb, ReportCallback reportCb) 
    : currentState(CommState::STARTUP), sendCallback(sendCb), reportCallback(reportCb) {
    resetConfigurationStatus();
}

void A429Communicator::resetConfigurationStatus() {
    for(bool &b : txConfigured) b = false;
    for(bool &b : rxConfigured) b = false;
}

void A429Communicator::update(std::chrono::steady_clock::time_point now) {
    // Process all buffered incoming messages
    while (!rxBuffer.empty()) {
        processPacket(rxBuffer.front(), now);
        rxCounter++;
        rxBuffer.pop_front();
    }

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

void A429Communicator::onPacketReceived(const A429Message& msg, std::chrono::steady_clock::time_point /*now*/) {
    rxBuffer.push_back(msg);
}

void A429Communicator::processPacket(const A429Message& msg, std::chrono::steady_clock::time_point now) {
    if (msg.type == MsgType::TX_CFG_STATUS) {
        lastConfigStatusReceived = now;
        for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
            A429Word status(msg.data[i]);
            if (status.status.configured) {
                txConfigured[i] = true;
            }
        }
        checkConfigurationComplete(now);
    }
    else if (msg.type == MsgType::RX_CFG_STATUS) {
        lastConfigStatusReceived = now;
        for (int i = 0; i < MAX_RX_CHANNELS; ++i) {
            A429Word status(msg.data[i]);
            if (status.status.configured) {
                rxConfigured[i] = true;
            }
        }
        checkConfigurationComplete(now);
    }
    else if (currentState == CommState::OPERATIONAL) {
        // Handle standard RX/TX data
    }
}

CommState A429Communicator::getCurrentState() const {
    return currentState;
}

void A429Communicator::checkConfigurationComplete(std::chrono::steady_clock::time_point now) {
    if (currentState != CommState::CONFIGURING) return;

    bool anyConfigured = false;
    for (bool b : txConfigured) if (b) anyConfigured = true;
    for (bool b : rxConfigured) if (b) anyConfigured = true;

    if (anyConfigured) {
        std::cout << "At least one channel configured. Entering OPERATIONAL state." << std::endl;
        currentState = CommState::OPERATIONAL;
        // Reset timers to prevent immediate timeout
        lastConfigStatusReceived = now;
        lastOmdReport = now;
    }
}

void A429Communicator::sendConfiguration() {
    std::cout << "Sending Configuration Message..." << std::endl;

    // Send TX Configuration
    A429Message txMsg;
    txMsg.type = MsgType::TX_CFG;
    bool sendTx = false;
    A429Word txCfg;
    txCfg.config.speed = 1; // High Speed
    for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
        if (!txConfigured[i]) {
            txMsg.data[i] = txCfg.raw;
            sendTx = true;
        }
    }
    if (sendTx) sendToHardware(txMsg);

    // Send RX Configuration
    A429Message rxMsg;
    rxMsg.type = MsgType::RX_CFG;
    bool sendRx = false;
    A429Word rxCfg;
    rxCfg.config.speed = 0; // Low Speed
    for (int i = 0; i < MAX_RX_CHANNELS; ++i) {
        if (!rxConfigured[i]) {
            rxMsg.data[i] = rxCfg.raw;
            sendRx = true;
        }
    }
    if (sendRx) sendToHardware(rxMsg);
}

void A429Communicator::sendToHardware(A429Message& msg) {
    msg.counter = txCounter++;
    if (sendCallback) sendCallback(msg);
}
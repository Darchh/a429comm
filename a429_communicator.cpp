#include "a429_communicator.h"
#include <vector>
#include <iostream>

void A429Communicator::bufferTx(const A429Message& msg) {
    // Buffer the message inside the class
    txQueue.push_back(msg);
    // You can process, log, or queue TX messages here
    std::cout << "[TX Buffer] TX Message Buffered. Type=" << (int)msg.type << " Counter=" << msg.counter << std::endl;
}

void A429Communicator::bufferRx(const A429Message& msg) {
    // Buffer the message inside the class
    rxQueue.push_back(msg);
    // You can process, log, or handle RX messages here
    std::cout << "[RX Buffer] RX Message Buffered. Type=" << (int)msg.type << " Counter=" << msg.counter << std::endl;
}

// Enable or disable TX channel
void A429Communicator::enableTxChannel(int channelIndex, bool enable) {
    if (channelIndex < 0 || channelIndex >= MAX_TX_CHANNELS) return;
    txChannels[channelIndex].configured = enable;
    if (enable) {
        txChannels[channelIndex].state = ChannelState::OPERATIONAL;
    } else {
        txChannels[channelIndex].state = ChannelState::IDLE;
    }
}
// Send message from one or more TX channels
void A429Communicator::sendMessage(const A429Message& msg, const std::vector<int>& channelIndices) {
    std::vector<int> targets = channelIndices;
    if (targets.empty()) {
        // If no channel is selected, send to all active channels
        for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
            if (txChannels[i].configured && txChannels[i].state == ChannelState::OPERATIONAL) {
                targets.push_back(i);
            }
        }
    }
    for (int channelIndex : targets) {
        if (channelIndex < 0 || channelIndex >= MAX_TX_CHANNELS) continue;
        if (!txChannels[channelIndex].configured || txChannels[channelIndex].state != ChannelState::OPERATIONAL) {
            std::cerr << "TX Channel " << channelIndex << " is not active. Message not sent." << std::endl;
            continue;
        }
        // TX message is passed to handler
        bufferTx(msg);
    }
}

A429Communicator::A429Communicator(SendCallback sendCb, ReceiveCallback receiveCb, ReportCallback reportCb, A429UdpDriver* udpDrv)
    : currentState(CommState::STARTUP), sendCallback(sendCb), receiveCallback(receiveCb), reportCallback(reportCb), udpDriver(udpDrv) {
    resetConfigurationStatus();
}

void A429Communicator::resetConfigurationStatus() {
    for(auto &ch : txChannels) {
        ch.state = ChannelState::IDLE;
        ch.configured = false;
    }
    for(auto &ch : rxChannels) {
        ch.state = ChannelState::IDLE;
        ch.configured = false;
    }
}

void A429Communicator::update(std::chrono::steady_clock::time_point now) {
    // Receive data from UDP
    if (udpDriver) {
        A429Message rxMsg;
        std::string senderIp;
        while (udpDriver->receive(rxMsg, senderIp)) {
            rxBuffer.push_back(rxMsg);
            std::cout << "Received packet from " << senderIp << std::endl;
        }
    }

    // Process all buffered incoming messages
    while (!rxBuffer.empty()) {
        processPacket(rxBuffer.front(), now);
        rxCounter++;
        rxBuffer.pop_front();
    }

    // Flush RX Queue to Application (Callback)
    // Consume RX messages accumulated in the buffer
    if (!rxQueue.empty()) {
        for (const auto& msg : rxQueue) {
            if (receiveCallback) {
                receiveCallback(msg);
            } else {
                defaultReceiveCallback(msg);
            }
        }
        rxQueue.clear();
    }

    // Flush TX Queue to UDP
    // Send buffered TX messages via UDP
    if (udpDriver && !txQueue.empty()) {
        for (const auto& msg : txQueue) {
            udpDriver->send(msg);
            if (sendCallback) sendCallback(msg);
        }
        txQueue.clear();
    }

    // Update each channel's state
    updateChannelStates(now);

    switch (currentState) {
        case CommState::STARTUP:
            sendConfiguration(now);
            currentState = CommState::CONFIGURING;
            break;

        case CommState::CONFIGURING:
            checkConfigurationComplete(now);
            break;

        case CommState::OPERATIONAL:
            // Operational Rule: TX/RX STATUS transmitted every 5s to OMD
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

void A429Communicator::processPacket(const A429Message& msg, std::chrono::steady_clock::time_point now) {
    if (msg.type == MsgType::TX_CFG_STATUS) {
        for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
            A429Word status(msg.data[i]);
            if (status.status.configured) {
                txChannels[i].configured = true;
                txChannels[i].state = ChannelState::OPERATIONAL;
                txChannels[i].lastStatusReceived = now;
                std::cout << "TX Channel " << i << " configured and operational." << std::endl;
            }
        }
        checkConfigurationComplete(now);
    }
    else if (msg.type == MsgType::RX_CFG_STATUS) {
        for (int i = 0; i < MAX_RX_CHANNELS; ++i) {
            A429Word status(msg.data[i]);
            if (status.status.configured) {
                rxChannels[i].configured = true;
                rxChannels[i].state = ChannelState::OPERATIONAL;
                rxChannels[i].lastStatusReceived = now;
                std::cout << "RX Channel " << i << " configured and operational." << std::endl;
            }
        }
        checkConfigurationComplete(now);
    }
    else if (currentState == CommState::OPERATIONAL) {
        // Handle standard RX/TX data
        if (msg.type == MsgType::TX) {
            bufferTx(msg);
        } else if (msg.type == MsgType::RX) {
            bufferRx(msg);
        }
    }
}

CommState A429Communicator::getCurrentState() const {
    return currentState;
}

void A429Communicator::checkConfigurationComplete(std::chrono::steady_clock::time_point now) {
    if (currentState != CommState::CONFIGURING) return;

    bool anyConfigured = false;
    for (const auto& ch : txChannels) {
        if (ch.configured) anyConfigured = true;
    }
    for (const auto& ch : rxChannels) {
        if (ch.configured) anyConfigured = true;
    }

    if (anyConfigured) {
        std::cout << "At least one channel configured. Entering OPERATIONAL state." << std::endl;
        currentState = CommState::OPERATIONAL;
        lastOmdReport = now;
    }
}

void A429Communicator::sendConfiguration(std::chrono::steady_clock::time_point now) {
    std::cout << "Sending Configuration Message..." << std::endl;

    // Send TX Configuration
    A429Message txMsg;
    txMsg.type = MsgType::TX_CFG;
    bool sendTx = false;
    A429Word txCfg;
    txCfg.config.speed = 1; // High Speed
    for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
        if (!txChannels[i].configured) {
            txMsg.data[i] = txCfg.raw;
            txChannels[i].state = ChannelState::CONFIGURING;
            txChannels[i].lastConfigSent = now;
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
        if (!rxChannels[i].configured) {
            rxMsg.data[i] = rxCfg.raw;
            rxChannels[i].state = ChannelState::CONFIGURING;
            rxChannels[i].lastConfigSent = now;
            sendRx = true;
        }
    }
    if (sendRx) sendToHardware(rxMsg);
}

void A429Communicator::sendToHardware(A429Message& msg) {
    msg.counter = txCounter++;
    
    // Pass to handler (buffer/log)
    bufferTx(msg);
}

void A429Communicator::updateChannelStates(std::chrono::steady_clock::time_point now) {
    // Check TX channels
    for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
        auto& ch = txChannels[i];
        
        if (ch.state == ChannelState::CONFIGURING) {
            // 4 second timeout - resend if no config response received
            if (std::chrono::duration_cast<std::chrono::seconds>(now - ch.lastConfigSent).count() >= 4) {
                std::cerr << "[Error] TX Channel " << i << " Configuration Timeout. Retrying..." << std::endl;
                sendChannelConfiguration(i, true, now);
                ch.lastConfigSent = now;
            }
        }
        else if (ch.state == ChannelState::OPERATIONAL) {
            // Connection lost if no status received for 4 seconds
            if (std::chrono::duration_cast<std::chrono::seconds>(now - ch.lastStatusReceived).count() >= 4) {
                std::cerr << "[Error] TX Channel " << i << " Connection Lost. Reverting to reconfiguration." << std::endl;
                ch.state = ChannelState::ERROR_RECOVERY;
                ch.configured = false;
            }
        }
        else if (ch.state == ChannelState::ERROR_RECOVERY) {
            // Reconfigure the channel
            ch.state = ChannelState::IDLE;
            sendChannelConfiguration(i, true, now);
        }
    }
    
    // Check RX channels
    for (int i = 0; i < MAX_RX_CHANNELS; ++i) {
        auto& ch = rxChannels[i];
        
        if (ch.state == ChannelState::CONFIGURING) {
            // 4 second timeout - resend if no config response received
            if (std::chrono::duration_cast<std::chrono::seconds>(now - ch.lastConfigSent).count() >= 4) {
                std::cerr << "[Error] RX Channel " << i << " Configuration Timeout. Retrying..." << std::endl;
                sendChannelConfiguration(i, false, now);
                ch.lastConfigSent = now;
            }
        }
        else if (ch.state == ChannelState::OPERATIONAL) {
            // Connection lost if no status received for 4 seconds
            if (std::chrono::duration_cast<std::chrono::seconds>(now - ch.lastStatusReceived).count() >= 4) {
                std::cerr << "[Error] RX Channel " << i << " Connection Lost. Reverting to reconfiguration." << std::endl;
                ch.state = ChannelState::ERROR_RECOVERY;
                ch.configured = false;
            }
        }
        else if (ch.state == ChannelState::ERROR_RECOVERY) {
            // Reconfigure the channel
            ch.state = ChannelState::IDLE;
            sendChannelConfiguration(i, false, now);
        }
    }
}

void A429Communicator::sendChannelConfiguration(int channelIndex, bool isTx, std::chrono::steady_clock::time_point now) {
    
    if (isTx) {
        A429Message txMsg;
        txMsg.type = MsgType::TX_CFG;
        A429Word txCfg;
        txCfg.config.speed = 1; // High Speed
        
        for (int i = 0; i < MAX_TX_CHANNELS; ++i) {
            txMsg.data[i] = DEFAULT_UNUSED_FIELD;
        }
        
        txMsg.data[channelIndex] = txCfg.raw;
        txChannels[channelIndex].state = ChannelState::CONFIGURING;
        txChannels[channelIndex].lastConfigSent = now;
        
        std::cout << "Sending TX Channel " << channelIndex << " configuration..." << std::endl;
        sendToHardware(txMsg);
    } else {
        A429Message rxMsg;
        rxMsg.type = MsgType::RX_CFG;
        A429Word rxCfg;
        rxCfg.config.speed = 0; // Low Speed
        
        for (int i = 0; i < MAX_RX_CHANNELS; ++i) {
            rxMsg.data[i] = DEFAULT_UNUSED_FIELD;
        }
        
        rxMsg.data[channelIndex] = rxCfg.raw;
        rxChannels[channelIndex].state = ChannelState::CONFIGURING;
        rxChannels[channelIndex].lastConfigSent = now;
        
        std::cout << "Sending RX Channel " << channelIndex << " configuration..." << std::endl;
        sendToHardware(rxMsg);
    }
}

void A429Communicator::defaultReceiveCallback(const A429Message& msg) {
    std::cout << "[Communicator] Received Message - Type: " << (int)msg.type << " Counter: " << msg.counter << std::endl;
    // Example: Pass data to Simulator variables
    // Simulator::instance().setArincData(msg.label, msg.data);
}
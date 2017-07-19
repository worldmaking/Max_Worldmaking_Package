// owl_rx.hpp -*- C++ -*-
// OWL C++ API v2.0

#ifndef OWL_RX_HPP
#define OWL_RX_HPP

#include <iostream>
#include <map>

#include <string.h>

namespace OWL {

  //// HubPacket ////

  struct HubPacket {
    uint8_t inputTTL;
    uint8_t payloadPage;
    uint8_t payload[64];
  };

  //// RXPacket ////

  struct RXPacket {

    enum { State=1, RFScan=2, RFData=3 };

    enum {
      CAMFLAGS_STATE = 1<<7,
      CAMFLAGS_RXSLOT = 1<<6,
      CAMFLAGS_RF_PIPE = 3<<0,
    };

    enum {
      PACKET_TYPE_MASK = 1<<7,
      PACKET_TYPE_SCAN = 0<<7,
      PACKET_TYPE_DATA = 1<<7
    };

    uint8_t hwid[2];
    uint8_t hwtype;
    uint8_t id;
    uint8_t rxMeta[4];
    uint8_t camFlags;
    uint8_t camStatus;
    uint8_t data[18];

    operator int() const { return (hwid[0] << 8) | (hwid[1] << 0); }

    int type() const;
  };

  //// CamStatePacket ////

  struct CamStatePacket {
    uint8_t ledSlot; // backup
    uint8_t rxSlot[3]; // 0:config, 1:state, 2:scan addr
    uint8_t txFlags[2]; // handshake for camera tx's
    uint8_t updateToggle;  // status of toggle  bits
    uint8_t rfTxAlternate; // state of rf tx masking
    uint8_t data[10];
  };

  //// RFScanPacket ////

  struct RFScanPacket {
    uint8_t type;
    uint8_t hwid[2];
    uint8_t hwtype;
    uint8_t rxAddr[2];
    uint8_t signalStatus;
    uint8_t batteryStatus;
    uint8_t encodeStatus;
    uint8_t readoutAddr;
    uint8_t readout[8];
  };

  //// RFDataPacket ////

  struct RFDataPacket {
    uint8_t type;
    uint8_t retry;
    uint8_t data[16];
  };

  //// RFDevice ////

  struct RFDevice {

    uint16_t hwid;
    uint8_t type;
    uint8_t id;

    int64_t lastUpdated, lastChanged;

    RFDevice();
    RFDevice(const RXPacket &rx, int64_t time);

    void update(const RXPacket &rx, int64_t time);

    bool isValid() const { return lastChanged > -1; }
  };

  //// RFDevices ////

  class RFDevices : public std::map<int,RFDevice> {
  public:

    int verbose;

    RFDevices();

    void update(const RXPacket &rx, int64_t time, int64_t timeout);
    void info(int64_t time=-1) const;
  };

  ////

  //// RXPacket ////

  inline int RXPacket::type() const
  {
    if(camFlags & CAMFLAGS_STATE) return State;
    if((camFlags & CAMFLAGS_RF_PIPE) != 0) return 0;
    return (data[0] & PACKET_TYPE_MASK) == PACKET_TYPE_SCAN ? RFScan : RFData;
  }

  //// RFDevice ////

  inline RFDevice::RFDevice() : hwid(0), type(-1), id(-1), lastUpdated(-1), lastChanged(-1)
  {
  }

  inline RFDevice::RFDevice(const RXPacket &rx, int64_t time) :
    hwid(rx), type(rx.hwtype), id(rx.id), lastUpdated(time), lastChanged(time)
  {
  }

  inline void RFDevice::update(const RXPacket &rx, int64_t time)
  {
    if(type != rx.hwtype || id != rx.id) lastChanged = time;
    type = rx.hwtype;
    id = rx.id;
    lastUpdated = time;
  }

  //// RFDevices ////

  inline RFDevices::RFDevices() : verbose(1)
  {
  }

  inline void RFDevices::update(const RXPacket &rx, int64_t time, int64_t timeout)
  {
    if((rx.type() == RXPacket::RFScan || rx.type() == RXPacket::RFData) && rx != 0)
      {
        // find or create device to update
        iterator i = find(rx);
        if(i == end())
          {
            i = insert(std::make_pair<>(rx, RFDevice(rx, time))).first;
            if(verbose) std::cout << "new device: " << std::hex << (i->second.hwid>>3) << std::dec << std::endl;
          }
        else i->second.update(rx, time);
      }

    // invalidate stale devices
    for(iterator i = begin(); timeout > 0 && i != end();)
      if(i->second.isValid() && i->second.lastUpdated + timeout < time)
        {
          if(verbose) std::cout << "remove device: " << std::hex << (i->second.hwid>>3) << std::dec << std::endl;
          erase(i++);
        }
      else i++;
  }

  inline void RFDevices::info(int64_t time) const
  {
    if(empty()) return;
    if(time < 0)
      {
        size_t count = 0;
        for(const_iterator i = begin(); i != end(); i++)
          if(i->second.isValid()) count++;
        std::cout << count << " device(s):" << std::endl;
      }
    for(const_iterator i = begin(); i != end(); i++)
      {
        const RFDevice &d = i->second;
        if(d.isValid())
          {
            if(time > -1 && time != d.lastChanged) continue;
            std::cout << "device hwid=0x" << std::hex << (d.hwid>>3) << std::dec
                      << " type=" << (int)d.type
                      << " id=" << (int)(char)d.id
                      << std::endl;
          }
      }
  }

  ////

} // namespace OWL

#endif // OWL_RX_HPP

// Copyright 2009-2019 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2019, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef _MERLIN_BRIDGE_H_
#define _MERLIN_BRIDGE_H_

#include <sst/core/component.h>
#include <sst/core/event.h>
#include <sst/core/interfaces/simpleNetwork.h>
#include <sst/core/output.h>

#include <deque>

namespace SST {
namespace Merlin {

using SST::Interfaces::SimpleNetwork;

class Bridge : public SST::Component {
   public:
    SST_ELI_REGISTER_COMPONENT(Bridge, "merlin", "Bridge", SST_ELI_ELEMENT_VERSION(1, 0, 0),
                               "Bridge between two memory networks.", COMPONENT_CATEGORY_NETWORK)

    SST_ELI_DOCUMENT_PARAMS(
        {"translator", "Translator backend.  Inherit from SST::Merlin::Bridge::Translator.",
         nullptr},
        {"debug", "0 (default): No debugging, 1: STDOUT, 2: STDERR, 3: FILE.", "0"},
        {"debug_level", "Debugging level: 0 to 10", "0"},
        {"network_bw", "The network link bandwidth.", "80GiB/s"},
        {"network_input_buffer_size", "Size of the network's input buffer.", "1KiB"},
        {"network_output_buffer_size", "Size of the network;s output buffer.", "1KiB"})

    SST_ELI_DOCUMENT_STATISTICS(
        {"pkts_received_net0", "Total number of packets recived on NIC0", "count", 1},
        {"pkts_received_net1", "Total number of packets recived on NIC1", "count", 1},
        {"pkts_sent_net0", "Total number of packets sent on NIC0", "count", 1},
        {"pkts_sent_net1", "Total number of packets sent on NIC1", "count", 1}, )

    SST_ELI_DOCUMENT_PORTS({"network0", "Network Link", {}}, {"network1", "Network Link", {}}, )

    Bridge(SST::ComponentId_t id, SST::Params &params);

    ~Bridge() override;

    void init(unsigned int /*phase*/) override;

    void setup() override;

    void finish() override;

    auto getAddrForNetwork(uint8_t netID) -> SimpleNetwork::nid_t {
        return interfaces[netID].getAddr();
    }

    class Translator : public SST::SubComponent {
       public:
        Translator(SST::Component *bridge, Params & /*params*/) : SubComponent(bridge) {}

        void init(unsigned int /*phase*/) override {}

        void setup() override {}

        void finish() override {}

        /**
         * Called when a network request is recieved.  Should return the corresponding
         * network request to be sent out on the opposite network.
         *
         * Return NULL if the packet should be be forwarded.
         */
        virtual auto translate(SimpleNetwork::Request *req, uint8_t fromNetwork)
            -> SimpleNetwork::Request * = 0;

        /**
         * Called when a network request is recieved during INIT.  Should return the corresponding
         * network request to be sent out on the opposite network.
         *
         * Return NULL if the packet should be be forwarded.
         */
        virtual auto initTranslate(SimpleNetwork::Request *req, uint8_t fromNetwork)
            -> SimpleNetwork::Request * = 0;

        auto getAddrForNetwork(uint8_t netID) -> SimpleNetwork::nid_t {
            return dynamic_cast<Bridge *>(parent)->getAddrForNetwork(netID);
        }
    };

   private:
    Output dbg;

    typedef std::map<std::string, SimpleNetwork::nid_t> addrMap_t;
    typedef std::map<std::string, uint64_t> imreMap_t;

    struct Nic_t {
        SimpleNetwork *nic{};
        std::deque<SimpleNetwork::Request *> sendQueue;

        Statistic<uint64_t> *stat_recv{};
        Statistic<uint64_t> *stat_send{};

        auto getAddr() const -> SimpleNetwork::nid_t { return nic->getEndpointID(); }
    };

    Nic_t interfaces[2];

    Translator *translator;

    void configureNIC(uint8_t id, SST::Params &params);

    auto handleIncoming(int vn, uint8_t id) -> bool;

    auto spaceAvailable(int vn, uint8_t id) -> bool;
};

}  // namespace Merlin
}  // namespace SST

#endif

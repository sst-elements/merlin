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

#include "trafficgen.h"

#include <sst/core/params.h>
#include <sst/core/simulation.h>
#include <sst/core/sst_config.h>
#include <sst/core/timeLord.h>
#include <unistd.h>

#include <climits>
#include <csignal>

#include "../linkControl.h"

using namespace SST::Merlin;
using namespace SST::Interfaces;

#if ENABLE_FINISH_HACK
int TrafficGen::count = 0;
int TrafficGen::received = 0;
int TrafficGen::min_lat = 0xffffffff;
int TrafficGen::max_lat = 0;
int TrafficGen::mean_sum = 0;
#endif

TrafficGen::TrafficGen(ComponentId_t cid, Params &params)
    : Component(cid),
      //    last_vc(0),
      packets_sent(0),
      packets_recd(0),
      done(false),
      packet_delay(0),
      packetDestGen(nullptr),
      packetSizeGen(nullptr),
      packetDelayGen(nullptr) {
    out.init(getName() + ": ", 0, 0, Output::STDOUT);

    id = params.find<int>("id", -1);
    if (id == -1) {
        out.fatal(CALL_INFO, -1, "id must be set!\n");
    }

    num_peers = params.find<int>("num_peers", -1);
    if (num_peers == -1) {
        out.fatal(CALL_INFO, -1, "num_peers must be set!\n");
    }

    // num_vns = params.find_integer("num_vns");
    // if ( num_vns == -1 ) {
    //     out.fatal(CALL_INFO, -1, "num_vns must be set!\n");
    // }
    num_vns = 1;

    std::string link_bw_s = params.find<std::string>("link_bw");
    if (link_bw_s.empty()) {
        out.fatal(CALL_INFO, -1, "link_bw must be set!\n");
    }
    // TimeConverter* tc = Simulation::getSimulation()->getTimeLord()->getTimeConverter(link_bw);

    UnitAlgebra link_bw(link_bw_s);

    addressMode = SEQUENTIAL;

    // Create a LinkControl object

    std::string buf_len = params.find<std::string>("buffer_length", "1kB");
    // NOTE:  This MUST be the same length as 'num_vns'
    // int *buf_size = new int[num_vns];
    // for ( int i = 0 ; i < num_vns ; i++ ) {
    //     buf_size[i] = buf_len;
    // }

    UnitAlgebra buf_size(buf_len);

    link_control =
        dynamic_cast<Merlin::LinkControl *>(loadSubComponent("merlin.linkcontrol", this, params));
    link_control->initialize("rtr", link_bw, num_vns, buf_len, buf_len);
    // delete [] buf_size;

    packets_to_send = params.find<uint64_t>("packets_to_send", 1000);

    /* Distribution selection */
    packetDestGen = buildGenerator("PacketDest", params);
    assert(packetDestGen);
    packetDestGen->seed(id);

    /* Packet size */
    // base_packet_size = params.find_integer("packet_size", 64); // In Bits
    packetSizeGen = buildGenerator("PacketSize", params);
    if (packetSizeGen != nullptr) {
        packetSizeGen->seed(id);
    }

    std::string packet_size_s = params.find<std::string>("packet_size", "8B");
    UnitAlgebra packet_size(packet_size_s);
    if (packet_size.hasUnits("B")) {
        packet_size *= UnitAlgebra("8b/B");
    }

    if (!packet_size.hasUnits("b")) {
        out.fatal(CALL_INFO, -1, "packet_size must be specified in units of either B or b!\n");
    }

    base_packet_size = packet_size.getRoundedValue();

    // base_packet_delay = params.find_integer("delay_between_packets", 0);
    packetDelayGen = buildGenerator("PacketDelay", params);
    if (packetDelayGen != nullptr) {
        packetDelayGen->seed(id);
    }

    std::string packet_delay_s = params.find<std::string>("delay_between_packets", "0s");
    UnitAlgebra packet_delay(packet_delay_s);

    if (!packet_delay.hasUnits("s")) {
        out.fatal(CALL_INFO, -1, "packet_delay must be specified in units of s!\n");
    }

    base_packet_delay = packet_delay.getRoundedValue();

    registerAsPrimaryComponent();
    primaryComponentDoNotEndSim();
    clock_functor = new Clock::Handler<TrafficGen>(this, &TrafficGen::clock_handler);
    clock_tc =
        registerClock(params.find<std::string>("message_rate", "1GHz"), clock_functor, false);

    // Register a receive handler which will simply strip the events as they arrive
    link_control->setNotifyOnReceive(
        new LinkControl::Handler<TrafficGen>(this, &TrafficGen::handle_receives));
    send_notify_functor = new LinkControl::Handler<TrafficGen>(this, &TrafficGen::send_notify);
}

TrafficGen::~TrafficGen() { delete link_control; }

auto TrafficGen::buildGenerator(const std::string &prefix, Params &params)
    -> TrafficGen::Generator * {
    Generator *gen = nullptr;
    std::string pattern = params.find<std::string>(prefix + ":pattern");
    std::pair<int, int> range = std::make_pair(params.find<int>(prefix + ":RangeMin", 0),
                                               params.find<int>(prefix + ":RangeMax", INT_MAX));

    auto rng_seed = params.find<uint32_t>(prefix + ":Seed", 1010101);

    if (pattern == "NearestNeighbor") {
        std::string shape = params.find<std::string>(prefix + ":NearestNeighbor:3DSize");
        int maxX;

        int maxY;

        int maxZ;
        assert(sscanf(shape.c_str(), "%d %d %d", &maxX, &maxY, &maxZ) == 3);
        gen = new NearestNeighbor(new UniformDist(0, 5), id, maxX, maxY, maxZ, 6);
    } else if (pattern == "Uniform") {
        gen = new UniformDist(range.first, range.second - 1);
    } else if (pattern == "HotSpot") {
        int target = params.find<int>(prefix + ":HotSpot:target");
        auto targetProb = params.find<float>(prefix + ":HotSpot:targetProbability");
        gen = new DiscreteDist(range.first, range.second, target, targetProb);
    } else if (pattern == "Normal") {
        auto mean = params.find<float>(prefix + ":Normal:Mean", range.second / 2.0F);
        auto sigma = params.find<float>(prefix + ":Normal:Sigma", 1.0F);
        gen = new NormalDist(range.first, range.second, mean, sigma);
    } else if (pattern == "Exponential") {
        auto lambda = params.find<float>(prefix + ":Exponential:Lambda", range.first);
        gen = new ExponentialDist(lambda);
    } else if (pattern == "Binomial") {
        int trials = params.find<int>(prefix + ":Binomial:Mean", range.second);
        auto probability = params.find<float>(prefix + ":Binomial:Sigma", 0.5F);
        gen = new BinomialDist(range.first, range.second, trials, probability);
    } else if (!pattern.empty()) {  // Allow none - non-pattern
        out.fatal(CALL_INFO, -1, "Unknown pattern '%s'\n", pattern.c_str());
    }

    if (gen != nullptr) {
        gen->seed(rng_seed);
    }

    return gen;
}

void TrafficGen::finish() { link_control->finish(); }

void TrafficGen::setup() {
    link_control->setup();
#if ENABLE_FINISH_HACK
    count++;
#endif
}

void TrafficGen::init(unsigned int phase) { return link_control->init(phase); }

auto TrafficGen::clock_handler(Cycle_t /*cycle*/) -> bool {
    if (done) {
        return true;
    }
    if (packets_sent >= packets_to_send) {
        // out.output("Node %d done sending.\n", id);
        primaryComponentOKToEndSim();
        done = true;
    }

    if (packet_delay != 0) {
        --packet_delay;
    } else {
        // Send packets
        if (packets_sent < packets_to_send) {
            int packet_size = getPacketSize();
            if (link_control->spaceToSend(0, packet_size)) {
                int target = getPacketDest();

                auto *req = new SimpleNetwork::Request();
                // req->givePayload(NULL);
                req->head = true;
                req->tail = true;

                switch (addressMode) {
                    case SEQUENTIAL:
                        req->dest = target;
                        req->src = id;
                        break;
                    case FATTREE_IP:
                        req->dest = fattree_ID_to_IP(target);
                        req->src = fattree_ID_to_IP(id);
                        break;
                }
                req->vn = 0;
                // ev->size_in_flits = packet_size;
                req->size_in_bits = packet_size;

                bool sent = link_control->send(req, 0);
                assert(sent);

                ++packets_sent;
            } else {
                link_control->setNotifyOnSend(send_notify_functor);
                return true;
            }
        }
        packet_delay = getDelayNextPacket();
    }

    return false;
}

auto TrafficGen::fattree_ID_to_IP(int id) -> int {
    union Addr {
        uint8_t x[4];
        int32_t s;
    };

    Addr addr{};

    int edge_switch = (id / ft_loading);
    int pod = edge_switch / (ft_radix / 2);
    int subnet = edge_switch % (ft_radix / 2);

    addr.x[0] = 10;
    addr.x[1] = pod;
    addr.x[2] = subnet;
    addr.x[3] = 2 + (id % ft_loading);

#if 0
    out.output("Converted NIC id %d to %u.%u.%u.%u.\n", id, addr.x[0], addr.x[1], addr.x[2], addr.x[3]\n);
#endif

    return addr.s;
}

auto TrafficGen::IP_to_fattree_ID(int ip) -> int {
    union Addr {
        uint8_t x[4];
        int32_t s;
    };

    Addr addr{};
    addr.s = ip;

    int id = 0;
    id += addr.x[1] * (ft_radix / 2) * ft_loading;
    id += addr.x[2] * ft_loading;
    id += addr.x[3] - 2;

    return id;
}

auto TrafficGen::handle_receives(int vn) -> bool {
    SimpleNetwork::Request *req = link_control->recv(vn);
    if (req != nullptr) {
        packets_recd++;
        delete req;
    }
    return true;
}

auto TrafficGen::send_notify(int /*vn*/) -> bool {
    reregisterClock(clock_tc, clock_functor);
    return false;
}

auto TrafficGen::getPacketDest() -> int {
    int dest = packetDestGen->getNextValue();
    assert(dest >= 0);
    return dest;
}

auto TrafficGen::getPacketSize() -> int {
    if (packetSizeGen != nullptr) {
        return packetSizeGen->getNextValue();
    }
    return base_packet_size;
}

auto TrafficGen::getDelayNextPacket() -> int {
    if (packetDelayGen != nullptr) {
        return packetDelayGen->getNextValue();
    }
    return base_packet_delay;
}

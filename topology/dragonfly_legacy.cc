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
//

#include "dragonfly_legacy.h"

#include <sst/core/sst_config.h>

#include <cstdlib>

using namespace SST::Merlin;

/*
 * Port Layout:
 * [0, params.p)                    // Hosts 0 -> params.p
 * [params.p, params.p+params.a-1)  // Routers within this group
 * [params.p+params.a-1, params.k)  // Other groups
 */

topo_dragonfly_legacy::topo_dragonfly_legacy(Component *comp, Params &p) : Topology(comp) {
    params.p = static_cast<uint32_t>(p.find<int>("dragonfly:hosts_per_router"));
    params.a = static_cast<uint32_t>(p.find<int>("dragonfly:routers_per_group"));
    params.k = static_cast<uint32_t>(p.find<int>("num_ports"));
    params.h = static_cast<uint32_t>(p.find<int>("dragonfly:intergroup_per_router"));
    params.g = static_cast<uint32_t>(p.find<int>("dragonfly:num_groups"));

    std::string route_algo = p.find<std::string>("dragonfly:algorithm", "minimal");

    if (route_algo == "valiant") {
        if (params.g <= 2) {
            /* 2 or less groups... no point in valiant */
            algorithm = MINIMAL;
        } else {
            algorithm = VALIANT;
        }
    } else {
        algorithm = MINIMAL;
    }

    uint32_t id = p.find<int>("id");
    group_id = id / params.a;
    router_id = id % params.a;
    output.verbose(CALL_INFO, 1, 1,
                   "%u:%u:  ID: %u   Params:  p = %u  a = %u  k = %u  h = %u  g = %u\n", group_id,
                   router_id, id, params.p, params.a, params.k, params.h, params.g);
}

topo_dragonfly_legacy::~topo_dragonfly_legacy() = default;

void topo_dragonfly_legacy::route(int port, int vc, internal_router_event *ev) {
    auto *td_ev = dynamic_cast<topo_dragonfly_legacy_event *>(ev);

    if (static_cast<uint32_t>(port) >= (params.p + params.a - 1)) {
        /* Came in from another group.  Increment VC */
        td_ev->setVC(vc + 1);
    }

    /* Minimal Route */
    uint32_t next_port = 0;
    if (td_ev->dest.group != group_id) {
        if (td_ev->dest.mid_group != group_id) {
            next_port = port_for_group(td_ev->dest.mid_group);
        } else {
            next_port = port_for_group(td_ev->dest.group);
        }
    } else if (td_ev->dest.router != router_id) {
        next_port = port_for_router(td_ev->dest.router);
    } else {
        next_port = td_ev->dest.host;
    }

    output.verbose(CALL_INFO, 1, 1, "%u:%u, Recv: %d/%d  Setting Next Port/VC:  %u/%u\n", group_id,
                   router_id, port, vc, next_port, td_ev->getVC());
    td_ev->setNextPort(next_port);
}

auto topo_dragonfly_legacy::process_input(RtrEvent *ev) -> internal_router_event * {
    dgnflyAddr dstAddr = {0, 0, 0, 0};
    idToLocation(ev->request->dest, &dstAddr);

    switch (algorithm) {
        case MINIMAL:
            dstAddr.mid_group = dstAddr.group;
            break;
        case VALIANT:
            if (dstAddr.group == group_id) {
                // staying here.
                dstAddr.mid_group = dstAddr.group;
            } else {
                do {
                    dstAddr.mid_group = rand() % params.g;
                } while (dstAddr.mid_group == group_id || dstAddr.mid_group == dstAddr.group);
            }
            break;
    }

    // output.verbose(CALL_INFO, 1, 1, "Init packet from %d to %d to %u:%u:%u:%u\n",
    // ev->request->src, ev->request->dest, dstAddr.group, dstAddr.mid_group, dstAddr.router,
    // dstAddr.host);

    auto *td_ev = new topo_dragonfly_legacy_event(dstAddr);
    td_ev->src_group = group_id;
    td_ev->setEncapsulatedEvent(ev);
    td_ev->setVC(ev->request->vn * 3);

    return td_ev;
}

void topo_dragonfly_legacy::routeInitData(int port, internal_router_event *ev,
                                          std::vector<int> &outPorts) {
    auto *td_ev = dynamic_cast<topo_dragonfly_legacy_event *>(ev);
    if (td_ev->dest.host == static_cast<uint32_t>(INIT_BROADCAST_ADDR)) {
        if (static_cast<uint32_t>(port) >= (params.p + params.a - 1)) {
            /* Came in from another group.
             * Send to locals, and other routers in group
             */
            for (uint32_t p = 0; p < (params.p + params.a - 1); p++) {
                outPorts.push_back(static_cast<int>(p));
            }
        } else if (static_cast<uint32_t>(port) >= params.p) {
            /* Came in from another router in group.
             * send to hosts
             * if this is the source group, send to other groups
             */
            for (uint32_t p = 0; p < params.p; p++) {
                outPorts.push_back(static_cast<int>(p));
            }
            if (td_ev->src_group == group_id) {
                for (uint32_t p = (params.p + params.a - 1); p < params.k; p++) {
                    outPorts.push_back(static_cast<int>(p));
                }
            }
        } else {
            /* Came in from a host
             * Send to all other hosts and routers in group, and all groups
             */
            for (int p = 0; p < static_cast<int>(params.k); p++) {
                if (p != port) {
                    outPorts.push_back(p);
                }
            }
        }
    } else {
        route(port, 0, ev);
        outPorts.push_back(ev->getNextPort());
    }
}

auto topo_dragonfly_legacy::process_InitData_input(RtrEvent *ev) -> internal_router_event * {
    dgnflyAddr dstAddr{};
    idToLocation(ev->request->dest, &dstAddr);
    auto *td_ev = new topo_dragonfly_legacy_event(dstAddr);
    td_ev->src_group = group_id;
    td_ev->setEncapsulatedEvent(ev);

    return td_ev;
}

auto topo_dragonfly_legacy::getPortState(int port) const -> Topology::PortState {
    if (static_cast<uint32_t>(port) < params.p) {
        return R2N;
    }

    return R2R;
}

auto topo_dragonfly_legacy::getPortLogicalGroup(int port) const -> std::string {
    if (static_cast<uint32_t>(port) < params.p) {
        return "host";
    }
    if (static_cast<uint32_t>(port) >= params.p &&
        static_cast<uint32_t>(port) < (params.p + params.a - 1)) {
        return "group";
    }

    return "global";
}

auto topo_dragonfly_legacy::getEndpointID(int port) -> int {
    return (group_id * (params.a /*rtr_per_group*/ * params.p /*hosts_per_rtr*/)) +
           (router_id * params.p /*hosts_per_rtr*/) + port;
}

void topo_dragonfly_legacy::idToLocation(int id, dgnflyAddr *location) const {
    if (id == INIT_BROADCAST_ADDR) {
        location->group = static_cast<uint32_t>(INIT_BROADCAST_ADDR);
        location->mid_group = static_cast<uint32_t>(INIT_BROADCAST_ADDR);
        location->router = static_cast<uint32_t>(INIT_BROADCAST_ADDR);
        location->host = static_cast<uint32_t>(INIT_BROADCAST_ADDR);
    } else {
        uint32_t hosts_per_group = params.p * params.a;
        location->group = id / hosts_per_group;
        location->router = (id % hosts_per_group) / params.p;
        location->host = id % params.p;
    }
}

auto topo_dragonfly_legacy::router_to_group(uint32_t group) const -> uint32_t {
    /* For now, assume only 1 connection to each group */
    if (group < group_id) {
        return group / params.h;
    }
    if (group > group_id) {
        return (group - 1) / params.h;
    }
    output.fatal(CALL_INFO, -1, "Trying to find router to own group.\n");
    return 0;
}

/* returns local router port if group can't be reached from this router */
auto topo_dragonfly_legacy::port_for_group(uint32_t group) const -> uint32_t {
    uint32_t tgt_rtr = router_to_group(group);
    if (tgt_rtr == router_id) {
        uint32_t port = params.p + params.a - 1;
        if (group < group_id) {
            port += (group % params.h);
        } else {
            port += ((group - 1) % params.h);
        }
        return port;
    }
    return port_for_router(tgt_rtr);
}

auto topo_dragonfly_legacy::port_for_router(uint32_t router) const -> uint32_t {
    uint32_t tgt = params.p + router;
    if (router > router_id) {
        tgt--;
    }
    return tgt;
}

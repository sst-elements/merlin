// -*- mode: c++ -*-

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

#ifndef COMPONENTS_MERLIN_TOPOLOGY_DRAGONFLY_LEGACY_H
#define COMPONENTS_MERLIN_TOPOLOGY_DRAGONFLY_LEGACY_H

#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/params.h>

#include "../router.h"

namespace SST {
namespace Merlin {

class topo_dragonfly_legacy_event;

class topo_dragonfly_legacy : public Topology {
   public:
    SST_ELI_REGISTER_SUBCOMPONENT(topo_dragonfly_legacy, "merlin", "dragonfly_legacy",
                                  SST_ELI_ELEMENT_VERSION(1, 0, 0),
                                  "Legacy dragonfly topology object.  No longer supported.",
                                  "SST::Merlin::Topology")

    SST_ELI_DOCUMENT_PARAMS({"dragonfly:hosts_per_router",
                             "Number of hosts connected to each router."},
                            {"dragonfly:routers_per_group",
                             "Number of links used to connect to routers in same group."},
                            {"dragonfly:intergroup_per_router",
                             "Number of links per router connected to other groups."},
                            {"dragonfly:num_groups", "Number of groups in network."},
                            {"dragonfly:algorithm",
                             "Routing algorithm to use [minmal (default) | valiant].", "minimal"})

    /* Assumed connectivity of each router:
     * ports [0, p-1]:      Hosts
     * ports [p, p+a-2]:    Intra-group
     * ports [p+a-1, k-1]:  Inter-group
     */

    struct dgnflyParams {
        uint32_t p; /* # of hosts / router */
        uint32_t a; /* # of routers / group */
        uint32_t k; /* Router Radix */
        uint32_t h; /* # of ports / router to connect to other groups */
        uint32_t g; /* # of Groups */
    };

    enum RouteAlgo { MINIMAL, VALIANT };

    struct dgnflyParams params {};
    RouteAlgo algorithm;
    uint32_t group_id;
    uint32_t router_id;

   public:
    struct dgnflyAddr {
        uint32_t group;
        uint32_t mid_group;
        uint32_t router;
        uint32_t host;
    };

    topo_dragonfly_legacy(Component *comp, Params &p);

    ~topo_dragonfly_legacy() override;

    void route(int port, int vc, internal_router_event *ev) override;

    auto process_input(RtrEvent *ev) -> internal_router_event * override;

    auto getPortState(int port) const -> PortState override;

    auto getPortLogicalGroup(int port) const -> std::string override;

    void routeInitData(int port, internal_router_event *ev, std::vector<int> &outPorts) override;

    auto process_InitData_input(RtrEvent *ev) -> internal_router_event * override;

    auto computeNumVCs(int vns) -> int override { return vns * 3; }

    auto getEndpointID(int port) -> int override;

   private:
    void idToLocation(int id, dgnflyAddr *location) const;

    auto router_to_group(uint32_t group) const -> uint32_t;

    auto port_for_router(uint32_t router) const -> uint32_t;

    auto port_for_group(uint32_t group) const -> uint32_t;
};

class topo_dragonfly_legacy_event : public internal_router_event {
   public:
    uint32_t src_group{};
    topo_dragonfly_legacy::dgnflyAddr dest{};

    topo_dragonfly_legacy_event() = default;

    explicit topo_dragonfly_legacy_event(const topo_dragonfly_legacy::dgnflyAddr &dest)
        : dest(dest) {}

    ~topo_dragonfly_legacy_event() override = default;

    auto clone() -> internal_router_event * override {
        return new topo_dragonfly_legacy_event(*this);
    }

    void serialize_order(SST::Core::Serialization::serializer &ser) override {
        internal_router_event::serialize_order(ser);
        ser &src_group;
        ser &dest.group;
        ser &dest.mid_group;
        ser &dest.router;
        ser &dest.host;
    }

   private:
    ImplementSerializable(SST::Merlin::topo_dragonfly_legacy_event)
};

}  // namespace Merlin
}  // namespace SST

#endif  // COMPONENTS_MERLIN_TOPOLOGY_DRAGONFLY_LEGACY_H

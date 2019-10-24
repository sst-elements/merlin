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

#ifndef COMPONENTS_MERLIN_TOPOLOGY_DRAGONFLY_H
#define COMPONENTS_MERLIN_TOPOLOGY_DRAGONFLY_H

#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/params.h>
#include <sst/core/rng/sstrng.h>

#include "../router.h"

namespace SST {
class SharedRegion;

namespace Merlin {

class topo_dragonfly_event;

struct RouterPortPair {
    uint16_t router{};
    uint16_t port{};

    RouterPortPair(int router, int port) : router(router), port(port) {}

    RouterPortPair() = default;
};

class RouteToGroup {
   private:
    const RouterPortPair *data{};
    SharedRegion *region{};
    size_t groups{};
    size_t routes{};

   public:
    RouteToGroup() = default;

    void init(SharedRegion *sr, size_t g, size_t r);

    auto getRouterPortPair(int group, int route_number) -> const RouterPortPair &;

    void setRouterPortPair(int group, int route_number, const RouterPortPair &pair);
};

class topo_dragonfly : public Topology {
   public:
    SST_ELI_REGISTER_SUBCOMPONENT(topo_dragonfly, "merlin", "dragonfly",
                                  SST_ELI_ELEMENT_VERSION(1, 0, 0),
                                  "Dragonfly topology object.  Implements a dragonfly with a "
                                  "single all to all pattern within the group.",
                                  "SST::Merlin::Topology")

    SST_ELI_DOCUMENT_PARAMS(
        {"dragonfly:hosts_per_router", "Number of hosts connected to each router."},
        {"dragonfly:routers_per_group",
         "Number of links used to connect to routers in same group."},
        {"dragonfly:intergroup_per_router",
         "Number of links per router connected to other groups."},
        {"dragonfly:intergroup_links", "Number of links between each pair of groups."},
        {"dragonfly:num_groups", "Number of groups in network."},
        {"dragonfly:algorithm", "Routing algorithm to use [minmal (default) | valiant].",
         "minimal"},
        {"dragonfly:adaptive_threshold", "Threshold to use when make adaptive routing decisions.",
         "2.0"},
        {"dragonfly:global_link_map",
         "Array specifying connectivity of global links in each dragonfly group."},
        {"dragonfly:global_route_mode",
         "Mode for intepreting global link map [absolute (default) | relative].", "absolute"}, )

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
        uint32_t n; /* # of links between groups in a pair */
    };

    enum RouteAlgo { MINIMAL, VALIANT, ADAPTIVE_LOCAL };

    RouteToGroup group_to_global_port;

    struct dgnflyParams params {};
    RouteAlgo algorithm;
    double adaptive_threshold;
    uint32_t group_id;
    uint32_t router_id;

    RNG::SSTRandom *rng;

    int const *output_credits{};
    int num_vcs{};

    enum global_route_mode_t { ABSOLUTE, RELATIVE };
    global_route_mode_t global_route_mode;

   public:
    struct dgnflyAddr {
        uint32_t group;
        uint32_t mid_group;
        uint32_t mid_group_shadow;
        uint32_t router;
        uint32_t host;
    };

    topo_dragonfly(Component *comp, Params &p);

    ~topo_dragonfly() override;

    void route(int port, int vc, internal_router_event *ev) override;

    void reroute(int port, int vc, internal_router_event *ev) override;

    auto process_input(RtrEvent *ev) -> internal_router_event * override;

    auto getPortState(int port) const -> PortState override;

    auto getPortLogicalGroup(int port) const -> std::string override;

    void routeInitData(int port, internal_router_event *ev, std::vector<int> &outPorts) override;

    auto process_InitData_input(RtrEvent *ev) -> internal_router_event * override;

    auto computeNumVCs(int vns) -> int override { return vns * 3; }

    auto getEndpointID(int port) -> int override;

    void setOutputBufferCreditArray(int const *array, int vcs) override;

   private:
    void idToLocation(int id, dgnflyAddr *location);

    auto router_to_group(uint32_t group) -> uint32_t;

    auto port_for_router(uint32_t router) -> uint32_t;

    auto port_for_group(uint32_t group, uint32_t global_slice, int id = -1) -> uint32_t;
};

class topo_dragonfly_event : public internal_router_event {
   public:
    uint32_t src_group{};
    topo_dragonfly::dgnflyAddr dest{};
    uint16_t global_slice{};
    uint16_t global_slice_shadow{};

    topo_dragonfly_event() = default;

    explicit topo_dragonfly_event(const topo_dragonfly::dgnflyAddr &dest) : dest(dest) {}

    ~topo_dragonfly_event() override = default;

    auto clone() -> internal_router_event * override { return new topo_dragonfly_event(*this); }

    void serialize_order(SST::Core::Serialization::serializer &ser) override {
        internal_router_event::serialize_order(ser);
        ser &src_group;
        ser &dest.group;
        ser &dest.mid_group;
        ser &dest.mid_group_shadow;
        ser &dest.router;
        ser &dest.host;
        ser &global_slice;
        ser &global_slice_shadow;
    }

   private:
    ImplementSerializable(SST::Merlin::topo_dragonfly_event)
};

}  // namespace Merlin
}  // namespace SST

#endif  // COMPONENTS_MERLIN_TOPOLOGY_DRAGONFLY_H

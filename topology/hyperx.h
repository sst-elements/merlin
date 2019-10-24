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

#ifndef COMPONENTS_MERLIN_TOPOLOGY_HYPERX_H
#define COMPONENTS_MERLIN_TOPOLOGY_HYPERX_H

#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/params.h>
#include <sst/core/rng/sstrng.h>

#include <cstring>
#include <vector>

#include "../router.h"

namespace SST {
namespace Merlin {

class topo_hyperx_event : public internal_router_event {
   public:
    int dimensions{};
    // First non aligned dimension
    int last_routing_dim{};
    int *dest_loc{};
    bool val_route_dest{};
    int *val_loc{};

    id_type id;
    bool rerouted{};

    topo_hyperx_event() = default;

    explicit topo_hyperx_event(int dim) : dimensions(dim), last_routing_dim(-1) {
        dest_loc = new int[dim];
        val_loc = new int[dim];
        id = generateUniqueId();
    }

    ~topo_hyperx_event() override {
        delete[] dest_loc;
        delete[] val_loc;
    }

    auto clone() -> internal_router_event * override {
        auto *tte = new topo_hyperx_event(*this);
        tte->dest_loc = new int[dimensions];
        memcpy(tte->dest_loc, dest_loc, dimensions * sizeof(int));
        return tte;
    }

    void getUnalignedDimensions(const int *curr_loc, std::vector<int> &dims) {
        for (int i = 0; i < dimensions; ++i) {
            if (dest_loc[i] != curr_loc[i]) {
                dims.push_back(i);
            }
        }
    }

    void serialize_order(SST::Core::Serialization::serializer &ser) override {
        internal_router_event::serialize_order(ser);
        ser &dimensions;
        ser &last_routing_dim;

        if (ser.mode() == SST::Core::Serialization::serializer::UNPACK) {
            dest_loc = new int[dimensions];
        }

        for (int i = 0; i < dimensions; i++) {
            ser &dest_loc[i];
        }

        if (ser.mode() == SST::Core::Serialization::serializer::UNPACK) {
            val_loc = new int[dimensions];
        }

        for (int i = 0; i < dimensions; i++) {
            ser &val_loc[i];
        }

        ser &val_route_dest;
        ser &id;
        ser &rerouted;
    }

   protected:
   private:
    ImplementSerializable(SST::Merlin::topo_hyperx_event)
};

class topo_hyperx_init_event : public topo_hyperx_event {
   public:
    int phase{};

    topo_hyperx_init_event() = default;

    explicit topo_hyperx_init_event(int dim) : topo_hyperx_event(dim) {}

    ~topo_hyperx_init_event() override = default;

    auto clone() -> internal_router_event * override {
        auto *tte = new topo_hyperx_init_event(*this);
        tte->dest_loc = new int[dimensions];
        tte->val_loc = new int[dimensions];
        memcpy(tte->dest_loc, dest_loc, dimensions * sizeof(int));
        return tte;
    }

    void serialize_order(SST::Core::Serialization::serializer &ser) override {
        topo_hyperx_event::serialize_order(ser);
        ser &phase;
    }

   private:
    ImplementSerializable(SST::Merlin::topo_hyperx_init_event)
};

class RNGFunc {
    RNG::SSTRandom *rng;

   public:
    explicit RNGFunc(RNG::SSTRandom *rng) : rng(rng) {}

    auto operator()(int i) -> int { return rng->generateNextUInt32() % i; }
};

class topo_hyperx : public Topology {
   public:
    SST_ELI_REGISTER_SUBCOMPONENT(topo_hyperx, "merlin", "hyperx", SST_ELI_ELEMENT_VERSION(0, 1, 0),
                                  "Multi-dimensional hyperx topology object",
                                  "SST::Merlin::Topology")

    SST_ELI_DOCUMENT_PARAMS({"hyperx:shape",
                             "Shape of the mesh specified as the number of routers in each "
                             "dimension, where each dimension is separated by a colon.  For "
                             "example, 4x4x2x2.  Any number of dimensions is supported."},
                            {"hyperx:width",
                             "Number of links between routers in each dimension, specified in same "
                             "manner as for shape.  For example, 2x2x1 denotes 2 links in the x "
                             "and y dimensions and one in the z dimension."},
                            {"hyperx:local_ports", "Number of endpoints attached to each router."},
                            {"hyperx:algorithm", "Routing algorithm to use.", "DOR"})

    enum RouteAlgo { DOR, DORND, MINA, VALIANT, DOAL, VDAL };

   private:
    int router_id;
    int *id_loc;

    int dimensions;
    int *dim_size;
    int *dim_width;
    int total_routers;

    int *port_start;  // where does each dimension start

    int num_local_ports;
    int local_port_start;

    int const *output_credits{};
    int const *output_queue_lengths{};
    int num_vcs{};

    RouteAlgo algorithm;
    RNG::SSTRandom *rng;
    RNGFunc *rng_func;

   public:
    topo_hyperx(Component *comp, Params &params);

    ~topo_hyperx() override;

    void route(int port, int vc, internal_router_event *ev) override;

    void reroute(int port, int vc, internal_router_event *ev) override;

    auto process_input(RtrEvent *ev) -> internal_router_event * override;

    void routeInitData(int port, internal_router_event *ev, std::vector<int> &outPorts) override;

    auto process_InitData_input(RtrEvent *ev) -> internal_router_event * override;

    auto getPortState(int port) const -> PortState override;

    auto computeNumVCs(int vns) -> int override;

    auto getEndpointID(int port) -> int override;

    void setOutputBufferCreditArray(int const *array, int vcs) override;

    void setOutputQueueLengthsArray(int const *array, int vcs) override;

   protected:
    virtual auto choose_multipath(int start_port, int num_ports) -> int;

   private:
    void idToLocation(int id, int *location) const;

    void parseDimString(const std::string &shape, int *output) const;

    auto get_dest_router(int dest_id) const -> int;

    auto get_dest_local_port(int dest_id) const -> int;

    auto routeDORBase(const int *dest_loc) -> std::pair<int, int>;

    void routeDOR(int port, int vc, topo_hyperx_event *ev);

    void routeDORND(int port, int vc, topo_hyperx_event *ev);

    void routeMINA(int port, int vc, topo_hyperx_event *ev);

    void routeDOAL(int port, int vc, topo_hyperx_event *ev);

    void routeVDAL(int port, int vc, topo_hyperx_event *ev);

    void routeValiant(int port, int vc, topo_hyperx_event *ev);
};

}  // namespace Merlin
}  // namespace SST

#endif  // COMPONENTS_MERLIN_TOPOLOGY_MESH_H

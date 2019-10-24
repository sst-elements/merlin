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

#ifndef COMPONENTS_MERLIN_TOPOLOGY_MESH_H
#define COMPONENTS_MERLIN_TOPOLOGY_MESH_H

#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/params.h>

#include <cstring>

#include "../router.h"

namespace SST {
namespace Merlin {

class topo_mesh_event : public internal_router_event {
   public:
    int dimensions{};
    int routing_dim{};
    int *dest_loc{};

    topo_mesh_event() = default;

    explicit topo_mesh_event(int dim) {
        dimensions = dim;
        routing_dim = 0;
        dest_loc = new int[dim];
    }

    ~topo_mesh_event() override { delete[] dest_loc; }

    auto clone() -> internal_router_event * override {
        auto *tte = new topo_mesh_event(*this);
        tte->dest_loc = new int[dimensions];
        memcpy(tte->dest_loc, dest_loc, dimensions * sizeof(int));
        return tte;
    }

    void serialize_order(SST::Core::Serialization::serializer &ser) override {
        internal_router_event::serialize_order(ser);
        ser &dimensions;
        ser &routing_dim;

        if (ser.mode() == SST::Core::Serialization::serializer::UNPACK) {
            dest_loc = new int[dimensions];
        }

        for (int i = 0; i < dimensions; i++) {
            ser &dest_loc[i];
        }
    }

   protected:
   private:
    ImplementSerializable(SST::Merlin::topo_mesh_event)
};

class topo_mesh_init_event : public topo_mesh_event {
   public:
    int phase{};

    topo_mesh_init_event() = default;

    explicit topo_mesh_init_event(int dim) : topo_mesh_event(dim) {}

    ~topo_mesh_init_event() override = default;

    auto clone() -> internal_router_event * override {
        auto *tte = new topo_mesh_init_event(*this);
        tte->dest_loc = new int[dimensions];
        memcpy(tte->dest_loc, dest_loc, dimensions * sizeof(int));
        return tte;
    }

    void serialize_order(SST::Core::Serialization::serializer &ser) override {
        topo_mesh_event::serialize_order(ser);
        ser &phase;
    }

   private:
    ImplementSerializable(SST::Merlin::topo_mesh_init_event)
};

class topo_mesh : public Topology {
   public:
    SST_ELI_REGISTER_SUBCOMPONENT(topo_mesh, "merlin", "mesh", SST_ELI_ELEMENT_VERSION(1, 0, 0),
                                  "Multi-dimensional mesh topology object", "SST::Merlin::Topology")

    SST_ELI_DOCUMENT_PARAMS({"mesh:shape",
                             "Shape of the mesh specified as the number of routers in each "
                             "dimension, where each dimension is separated by a colon.  For "
                             "example, 4x4x2x2.  Any number of dimensions is supported."},
                            {"mesh:width",
                             "Number of links between routers in each dimension, specified in same "
                             "manner as for shape.  For example, 2x2x1 denotes 2 links in the x "
                             "and y dimensions and one in the z dimension."},
                            {"mesh:local_ports", "Number of endpoints attached to each router."})

   private:
    int router_id;
    int *id_loc;

    int dimensions;
    int *dim_size;
    int *dim_width;

    int (*port_start)[2];  // port_start[dim][direction: 0=pos, 1=neg]

    int num_local_ports;
    int local_port_start;

   public:
    topo_mesh(Component *comp, Params &params);

    ~topo_mesh() override;

    void route(int port, int vc, internal_router_event *ev) override;

    auto process_input(RtrEvent *ev) -> internal_router_event * override;

    void routeInitData(int port, internal_router_event *ev, std::vector<int> &outPorts) override;

    auto process_InitData_input(RtrEvent *ev) -> internal_router_event * override;

    auto getPortState(int port) const -> PortState override;

    auto computeNumVCs(int vns) -> int override;

    auto getEndpointID(int port) -> int override;

   protected:
    virtual auto choose_multipath(int start_port, int num_ports, int dest_dist) -> int;

   private:
    void idToLocation(int id, int *location) const;

    void parseDimString(const std::string &shape, int *output) const;

    auto get_dest_router(int dest_id) const -> int;

    auto get_dest_local_port(int dest_id) const -> int;
};

}  // namespace Merlin
}  // namespace SST

#endif  // COMPONENTS_MERLIN_TOPOLOGY_MESH_H

// -*- mode: c++ -*-

// Copyright 2009-2020 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2020, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef COMPONENTS_MERLIN_TOPOLOGY_SINGLEROUTER_H
#define COMPONENTS_MERLIN_TOPOLOGY_SINGLEROUTER_H

#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/params.h>

#include "../router.h"

namespace SST {
namespace Merlin {

class topo_singlerouter : public Topology {

  public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(topo_singlerouter, "merlin", "singlerouter", SST_ELI_ELEMENT_VERSION(1, 0, 0),
                                          "Simple, single-router topology object", SST::Merlin::Topology)

  private:
    int num_ports;

  public:
    topo_singlerouter(ComponentId_t cid, Params &params, int num_ports, int rtr_id);
    ~topo_singlerouter() override;

    void route(int port, int vc, internal_router_event *ev) override;
    internal_router_event *process_input(RtrEvent *ev) override;

    void routeInitData(int port, internal_router_event *ev, std::vector<int> &outPorts) override;
    internal_router_event *process_InitData_input(RtrEvent *ev) override;

    PortState getPortState(int port) const override;

    int getEndpointID(int port) override { return port; }
};

} // namespace Merlin
} // namespace SST

#endif // COMPONENTS_MERLIN_TOPOLOGY_SINGLEROUTER_H

/*
 *    Copyright (c) 2020, The OpenThread Authors.
 *    All rights reserved.
 *
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the following conditions are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *    3. Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *    POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   The file implements the Thread Backbone agent.
 */

#include "backbone/backbone_agent.hpp"

#include <openthread/backbone_router_ftd.h>

#include "backbone/backbone_helper.hpp"
#include "common/code_utils.hpp"

namespace otbr {

namespace Backbone {

BackboneAgent::BackboneAgent(otbr::Ncp::ControllerOpenThread &aThread)
    : mThread(aThread)
    , mBackboneRouterState(OT_BACKBONE_ROUTER_STATE_DISABLED)
{
}

void BackboneAgent::Init(const std::string &aThreadIfName, const std::string &aBackboneIfName)
{
    mSMCRouteManager.Init(aThreadIfName, aBackboneIfName);

    HandleBackboneRouterState();
}

void BackboneAgent::HandleBackboneRouterState(void)
{
    otBackboneRouterState state      = otBackboneRouterGetState(mThread.GetInstance());
    bool                  wasPrimary = (mBackboneRouterState == OT_BACKBONE_ROUTER_STATE_PRIMARY);

    Log(OTBR_LOG_DEBUG, "HandleBackboneRouterState: state=%d, mBackboneRouterState=%d", state, mBackboneRouterState);
    VerifyOrExit(mBackboneRouterState != state);

    mBackboneRouterState = state;

    if (!wasPrimary && IsPrimary())
    {
        OnBecomePrimary();
    }
    else if (wasPrimary && !IsPrimary())
    {
        OnResignPrimary();
    }

exit:
    return;
}

void BackboneAgent::Log(int aLevel, const char *aFormat, ...)
{
    va_list ap;

    va_start(ap, aFormat);
    BackboneHelper::Logv(aLevel, "BackboneAgent", aFormat, ap);
    va_end(ap);
}

void BackboneAgent::OnBecomePrimary(void)
{
    Log(OTBR_LOG_NOTICE, "Backbone becomes Primary!");

    mSMCRouteManager.Enable();
}

void BackboneAgent::OnResignPrimary(void)
{
    Log(OTBR_LOG_NOTICE, "Backbone resigns Primary to %s!",
        BackboneHelper::BackboneRouterStateToString(mBackboneRouterState));

    mSMCRouteManager.Disable();
}

void BackboneAgent::HandleBackboneRouterMulticastListenerEvent(otBackboneRouterMulticastListenerEvent aEvent,
                                                               const otIp6Address &                   aAddress)
{
    Log(OTBR_LOG_INFO, "Multicast Listener event: %d, address: %s, IsPrimary: %s", aEvent,
        Ip6Address(aAddress).ToLongString().c_str(), IsPrimary() ? "Y" : "N");

    VerifyOrExit(IsPrimary());

    switch (aEvent)
    {
    case OT_BACKBONE_ROUTER_MULTICAST_LISTENER_ADDED:
        mSMCRouteManager.Add(Ip6Address(aAddress));
        break;
    case OT_BACKBONE_ROUTER_MULTICAST_LISTENER_REMOVED:
        mSMCRouteManager.Remove(Ip6Address(aAddress));
        break;
    }

exit:
    return;
}

} // namespace Backbone

} // namespace otbr

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
 *   The file implements the Multicast Routing manager.
 */

#include "backbone_router/multicast_routing.hpp"

#include <assert.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#if __linux__
#include <linux/mroute6.h>
#else
#error "Multicast Routing feature is not ported to non-Linux platforms yet."
#endif

#include "common/code_utils.hpp"

namespace otbr {

namespace BackboneRouter {

void MulticastRoutingManager::Enable(void)
{
    otbrError error = OTBR_ERROR_NONE;

    VerifyOrExit(!IsEnabled());

    SuccessOrExit(error = InitMulticastRouterSock());

exit:
    otbrLogResult(error, "MulticastRoutingManager: %s", __FUNCTION__);
}

void MulticastRoutingManager::Disable(void)
{
    otbrError error = OTBR_ERROR_NONE;

    VerifyOrExit(IsEnabled());

    FinalizeMulticastRouterSock();

exit:
    otbrLogResult(error, "MulticastRoutingManager: %s", __FUNCTION__);
}

void MulticastRoutingManager::Add(const Ip6Address &aAddress)
{
    assert(mListenerSet.find(aAddress) == mListenerSet.end());
    mListenerSet.insert(aAddress);

    VerifyOrExit(IsEnabled());

    UnblockInboundMulticastForwardingCache(aAddress);

exit:
    otbrLogResult(OTBR_ERROR_NONE, "MulticastRoutingManager: %s: %s", __FUNCTION__, aAddress.ToString().c_str());
}

void MulticastRoutingManager::Remove(const Ip6Address &aAddress)
{
    otbrError error = OTBR_ERROR_NONE;

    assert(mListenerSet.find(aAddress) != mListenerSet.end());
    mListenerSet.erase(aAddress);

    VerifyOrExit(IsEnabled());

    RemoveInboundMulticastForwardingCache(aAddress);

exit:
    otbrLogResult(error, "MulticastRoutingManager: %s: %s", __FUNCTION__, aAddress.ToString().c_str());
}

void MulticastRoutingManager::UpdateFdSet(fd_set & aReadFdSet,
                                          fd_set & aWriteFdSet,
                                          fd_set & aErrorFdSet,
                                          int &    aMaxFd,
                                          timeval &aTimeout) const
{
    OTBR_UNUSED_VARIABLE(aWriteFdSet);
    OTBR_UNUSED_VARIABLE(aErrorFdSet);
    OTBR_UNUSED_VARIABLE(aTimeout);

    VerifyOrExit(IsEnabled());

    FD_SET(mMulticastRouterSock, &aReadFdSet);
    aMaxFd = std::max(aMaxFd, mMulticastRouterSock);

exit:
    return;
}

void MulticastRoutingManager::Process(const fd_set &aReadFdSet, const fd_set &aWriteFdSet, const fd_set &aErrorFdSet)
{
    OTBR_UNUSED_VARIABLE(aReadFdSet);
    OTBR_UNUSED_VARIABLE(aWriteFdSet);
    OTBR_UNUSED_VARIABLE(aErrorFdSet);

    VerifyOrExit(IsEnabled());

    if (FD_ISSET(mMulticastRouterSock, &aReadFdSet))
    {
        ProcessMulticastRouterMessages();
    }

exit:
    return;
}

otbrError MulticastRoutingManager::InitMulticastRouterSock(void)
{
    otbrError           error = OTBR_ERROR_NONE;
    int                 one   = 1;
    struct icmp6_filter filter;
    struct mif6ctl      mif6ctl;

    VerifyOrExit(mMulticastRouterSock == -1);

    // Create a Multicast Routing socket
    mMulticastRouterSock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    VerifyOrExit(mMulticastRouterSock != -1, error = OTBR_ERROR_ERRNO);

    // Enable Multicast Forwarding in Kernel
    VerifyOrExit(0 == setsockopt(mMulticastRouterSock, IPPROTO_IPV6, MRT6_INIT, &one, sizeof(one)),
                 error = OTBR_ERROR_ERRNO);

    // Filter all ICMPv6 messages
    ICMP6_FILTER_SETBLOCKALL(&filter);
    VerifyOrExit(0 == setsockopt(mMulticastRouterSock, IPPROTO_ICMPV6, ICMP6_FILTER, (void *)&filter, sizeof(filter)),
                 error = OTBR_ERROR_ERRNO);

    memset(&mif6ctl, 0, sizeof(mif6ctl));
    mif6ctl.mif6c_flags     = 0;
    mif6ctl.vifc_threshold  = 1;
    mif6ctl.vifc_rate_limit = 0;

    // Add Thread network interface to MIF
    mif6ctl.mif6c_mifi = kMifIndexThread;
    mif6ctl.mif6c_pifi = if_nametoindex(InstanceParams::Get().GetThreadIfName());
    VerifyOrExit(mif6ctl.mif6c_pifi > 0, error = OTBR_ERROR_ERRNO);
    VerifyOrExit(0 == setsockopt(mMulticastRouterSock, IPPROTO_IPV6, MRT6_ADD_MIF, &mif6ctl, sizeof(mif6ctl)),
                 error = OTBR_ERROR_ERRNO);

    // Add Backbone network interface to MIF
    mif6ctl.mif6c_mifi = kMifIndexBackbone;
    mif6ctl.mif6c_pifi = if_nametoindex(InstanceParams::Get().GetBackboneIfName());
    VerifyOrExit(mif6ctl.mif6c_pifi > 0, error = OTBR_ERROR_ERRNO);
    VerifyOrExit(0 == setsockopt(mMulticastRouterSock, IPPROTO_IPV6, MRT6_ADD_MIF, &mif6ctl, sizeof(mif6ctl)),
                 error = OTBR_ERROR_ERRNO);

exit:
    if (error != OTBR_ERROR_NONE)
    {
        FinalizeMulticastRouterSock();
    }

    return error;
}

void MulticastRoutingManager::FinalizeMulticastRouterSock(void)
{
    VerifyOrExit(mMulticastRouterSock != -1);

    close(mMulticastRouterSock);
    mMulticastRouterSock = -1;

exit:
    return;
}

void MulticastRoutingManager::ProcessMulticastRouterMessages(void)
{
    otbrError       error = OTBR_ERROR_NONE;
    char            buf[128];
    int             nr;
    struct mrt6msg *mrt6msg;
    Ip6Address      src, dst;

    nr = read(mMulticastRouterSock, buf, sizeof(buf));

    VerifyOrExit(nr > 0, error = OTBR_ERROR_ERRNO);

    mrt6msg = reinterpret_cast<struct mrt6msg *>(buf);

    VerifyOrExit(mrt6msg->im6_mbz == 0);
    VerifyOrExit(mrt6msg->im6_msgtype == MRT6MSG_NOCACHE);

    src.Set(mrt6msg->im6_src);
    dst.Set(mrt6msg->im6_dst);

    error = AddMulticastForwardingCache(src, dst, static_cast<MifIndex>(mrt6msg->im6_mif));

exit:
    otbrLogResult(error, "MulticastRoutingManager: %s", __FUNCTION__);
}

otbrError MulticastRoutingManager::AddMulticastForwardingCache(const Ip6Address &aSrcAddr,
                                                               const Ip6Address &aGroupAddr,
                                                               MifIndex          aIif)
{
    otbrError      error = OTBR_ERROR_NONE;
    struct mf6cctl mf6cctl;
    MifIndex       forwardMif = kMifIndexNone;

    VerifyOrExit(aIif == kMifIndexThread || aIif == kMifIndexBackbone, error = OTBR_ERROR_INVALID_ARGS);

    ExpireMulticastForwardingCache();

    if (aIif == kMifIndexBackbone)
    {
        // Forward multicast traffic form Backbone to Thread if the group address is subscribed by MLR.
        if (mListenerSet.find(aGroupAddr) != mListenerSet.end())
        {
            forwardMif = kMifIndexThread;
        }
    }
    else
    {
        // Forward multicast traffic from Thread to Backbone if multicast scope > kRealmLocalScope
        if (aGroupAddr.GetScope() > Ip6Address::kRealmLocalScope)
        {
            forwardMif = kMifIndexBackbone;
        }
    }

    memset(&mf6cctl, 0, sizeof(mf6cctl));
    aSrcAddr.CopyTo(mf6cctl.mf6cc_origin);
    aGroupAddr.CopyTo(mf6cctl.mf6cc_mcastgrp);
    mf6cctl.mf6cc_parent = aIif;

    if (forwardMif != kMifIndexNone)
    {
        IF_SET(forwardMif, &mf6cctl.mf6cc_ifset);
    }

    VerifyOrExit(0 == setsockopt(mMulticastRouterSock, IPPROTO_IPV6, MRT6_ADD_MFC, &mf6cctl, sizeof(mf6cctl)),
                 error = OTBR_ERROR_ERRNO);

    mMulticastForwardingCache[MulticastRoute(aSrcAddr, aGroupAddr)] = MulticastRouteInfo(aIif, forwardMif);

exit:
    otbrLogResult(error, "MulticastRoutingManager: %s: add dynamic route for %s => %s, MIF=%s, ForwardMIF=%s",
                  __FUNCTION__, aSrcAddr.ToString().c_str(), aGroupAddr.ToString().c_str(), MifIndexToString(aIif),
                  MifIndexToString(forwardMif));

    return error;
}

void MulticastRoutingManager::UnblockInboundMulticastForwardingCache(const Ip6Address &aGroupAddr)
{
    struct mf6cctl mf6cctl;

    memset(&mf6cctl, 0, sizeof(mf6cctl));
    aGroupAddr.CopyTo(mf6cctl.mf6cc_mcastgrp);
    mf6cctl.mf6cc_parent = kMifIndexBackbone;
    IF_SET(kMifIndexThread, &mf6cctl.mf6cc_ifset);

    for (const std::pair<const MulticastRoute, MulticastRouteInfo> &entry : mMulticastForwardingCache)
    {
        otbrError                 error;
        const MulticastRoute &    route     = entry.first;
        const MulticastRouteInfo &routeInfo = entry.second;

        if (routeInfo.mIif != kMifIndexBackbone || routeInfo.mOif == kMifIndexThread || route.mGroupAddr != aGroupAddr)
        {
            continue;
        }

        // Unblock this inbound route
        route.mSrcAddr.CopyTo(mf6cctl.mf6cc_origin);

        error = (0 == setsockopt(mMulticastRouterSock, IPPROTO_IPV6, MRT6_ADD_MFC, &mf6cctl, sizeof(mf6cctl)))
                    ? OTBR_ERROR_NONE
                    : OTBR_ERROR_ERRNO;

        if (error == OTBR_ERROR_NONE)
        {
            mMulticastForwardingCache[route] = MulticastRouteInfo(routeInfo.mIif, kMifIndexThread);
        }

        otbrLogResult(error, "MulticastRoutingManager: %s: %s => %s, MIF=%s, ForwardMif=%s", __FUNCTION__,
                      route.mSrcAddr.ToString().c_str(), route.mGroupAddr.ToString().c_str(),
                      MifIndexToString(routeInfo.mIif), MifIndexToString(kMifIndexThread));
    }
}

void MulticastRoutingManager::RemoveInboundMulticastForwardingCache(const Ip6Address &aGroupAddr)
{
    struct mf6cctl mf6cctl;

    memset(&mf6cctl, 0, sizeof(mf6cctl));
    mf6cctl.mf6cc_parent = kMifIndexBackbone;
    aGroupAddr.CopyTo(mf6cctl.mf6cc_mcastgrp);

    for (std::map<MulticastRoute, MulticastRouteInfo>::const_iterator it = mMulticastForwardingCache.begin();
         it != mMulticastForwardingCache.end();)
    {
        otbrError                 error;
        const MulticastRoute &    route     = it->first;
        const MulticastRouteInfo &routeInfo = it->second;
        bool                      erase     = false;

        if (routeInfo.mIif == kMifIndexBackbone && route.mGroupAddr == aGroupAddr)
        {
            route.mSrcAddr.CopyTo(mf6cctl.mf6cc_origin);

            error = (0 == setsockopt(mMulticastRouterSock, IPPROTO_IPV6, MRT6_DEL_MFC, &mf6cctl, sizeof(mf6cctl)))
                        ? OTBR_ERROR_NONE
                        : OTBR_ERROR_ERRNO;

            if (error == OTBR_ERROR_NONE || errno == ENOENT)
            {
                erase = true;
            }

            otbrLogResult(error, "MulticastRoutingManager: %s: %s => %s, MIF=%s, ForwardMIF=%s", __FUNCTION__,
                          route.mSrcAddr.ToString().c_str(), route.mGroupAddr.ToString().c_str(),
                          MifIndexToString(routeInfo.mIif), MifIndexToString(kMifIndexNone));
        }

        if (erase)
        {
            mMulticastForwardingCache.erase(it++);
        }
        else
        {
            it++;
        }
    }

    mMulticastForwardingCache.clear();
}

void MulticastRoutingManager::ExpireMulticastForwardingCache(void)
{
    struct sioc_sg_req6            sioc_sg_req6;
    std::chrono::time_point<Clock> now = Clock::now();
    struct mf6cctl                 mf6cctl;

    memset(&mf6cctl, 0, sizeof(mf6cctl));

    memset(&sioc_sg_req6, 0, sizeof(sioc_sg_req6));

    for (std::map<MulticastRoute, MulticastRouteInfo>::const_iterator it = mMulticastForwardingCache.begin();
         it != mMulticastForwardingCache.end();)
    {
        otbrError             error;
        const MulticastRoute &route     = it->first;
        MulticastRouteInfo    routeInfo = it->second;
        bool                  erase     = false;

        if ((routeInfo.mLastUseTime + std::chrono::seconds(kMulticastForwardingCacheExpireTimeout) < now))
        {
            if (!UpdateMulticastRouteInfo(route))
            {
                // The multicast route is expired

                route.mSrcAddr.CopyTo(mf6cctl.mf6cc_origin);
                route.mGroupAddr.CopyTo(mf6cctl.mf6cc_mcastgrp);
                mf6cctl.mf6cc_parent = routeInfo.mIif;

                error = (0 == setsockopt(mMulticastRouterSock, IPPROTO_IPV6, MRT6_DEL_MFC, &mf6cctl, sizeof(mf6cctl)))
                            ? OTBR_ERROR_NONE
                            : OTBR_ERROR_ERRNO;

                if (error == OTBR_ERROR_NONE || errno == ENOENT)
                {
                    erase = true;
                }

                otbrLogResult(error, "MulticastRoutingManager: %s: %s => %s, MIF=%s, ForwardMIF=%s", __FUNCTION__,
                              route.mSrcAddr.ToString().c_str(), route.mGroupAddr.ToString().c_str(),
                              MifIndexToString(routeInfo.mIif), MifIndexToString(routeInfo.mOif));
            }
        }

        if (erase)
        {
            mMulticastForwardingCache.erase(it++);
        }
        else
        {
            it++;
        }
    }

    DumpMulticastForwardingCache();
}

bool MulticastRoutingManager::UpdateMulticastRouteInfo(const MulticastRoute &route)
{
    bool                updated = false;
    struct sioc_sg_req6 sioc_sg_req6;
    memset(&sioc_sg_req6, 0, sizeof(sioc_sg_req6));

    route.mSrcAddr.CopyTo(sioc_sg_req6.src);
    route.mGroupAddr.CopyTo(sioc_sg_req6.grp);

    MulticastRouteInfo &routeInfo = mMulticastForwardingCache[route];

    if (ioctl(mMulticastRouterSock, SIOCGETSGCNT_IN6, &sioc_sg_req6) != -1)
    {
        unsigned long validPktCnt;

        otbrLog(OTBR_LOG_DEBUG,
                "MulticastRoutingManager: %s: SIOCGETSGCNT_IN6 %s => %s: bytecnt=%lu, pktcnt=%lu, wrong_if=%lu",
                __FUNCTION__, route.mSrcAddr.ToString().c_str(), route.mGroupAddr.ToString().c_str(),
                sioc_sg_req6.bytecnt, sioc_sg_req6.pktcnt, sioc_sg_req6.wrong_if);

        validPktCnt = sioc_sg_req6.pktcnt - sioc_sg_req6.wrong_if;
        if (validPktCnt != routeInfo.mValidPktCnt)
        {
            routeInfo.mValidPktCnt = sioc_sg_req6.pktcnt;
            routeInfo.mLastUseTime = Clock::now();

            updated = true;
        }
    }
    else
    {
        otbrLog(OTBR_LOG_WARNING, "MulticastRoutingManager: %s: SIOCGETSGCNT_IN6 %s => %s failed: %s", __FUNCTION__,
                route.mSrcAddr.ToString().c_str(), route.mGroupAddr.ToString().c_str(), strerror(errno));
    }

    return updated;
}

const char *MulticastRoutingManager::MifIndexToString(MifIndex aMif)
{
    const char *string = "Unknown";

    switch (aMif)
    {
    case kMifIndexNone:
        string = "None";
        break;
    case kMifIndexThread:
        string = "Thread";
        break;
    case kMifIndexBackbone:
        string = "Backbone";
        break;
    }

    return string;
}

void MulticastRoutingManager::DumpMulticastForwardingCache(void)
{
    otbrLog(OTBR_LOG_DEBUG, "MulticastRoutingManager: ==================== MFC %d entries ====================",
            mMulticastForwardingCache.size());

    for (const std::pair<const MulticastRoute, MulticastRouteInfo> &entry : mMulticastForwardingCache)
    {
        const MulticastRoute &    route     = entry.first;
        const MulticastRouteInfo &routeInfo = entry.second;

        otbrLog(OTBR_LOG_DEBUG, "MulticastRoutingManager: %s %s => %s %s", MifIndexToString(routeInfo.mIif),
                route.mSrcAddr.ToString().c_str(), route.mGroupAddr.ToString().c_str(),
                MifIndexToString(routeInfo.mOif));
    }

    otbrLog(OTBR_LOG_DEBUG, "MulticastRoutingManager: ========================================================",
            mMulticastForwardingCache.size());
}

bool MulticastRoutingManager::MulticastRoute::operator<(const MulticastRoutingManager::MulticastRoute &aOther) const
{
    if (mGroupAddr != aOther.mGroupAddr)
    {
        return mGroupAddr < aOther.mGroupAddr;
    }

    if (mSrcAddr != aOther.mSrcAddr)
    {
        return mSrcAddr < aOther.mSrcAddr;
    }

    return false;
}

} // namespace BackboneRouter

} // namespace otbr

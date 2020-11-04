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
 *   This file includes definition for multicast routing management.
 */

#ifndef BACKBONE_ROUTER_MULTICAST_ROUTING_HPP_
#define BACKBONE_ROUTER_MULTICAST_ROUTING_HPP_

#include <set>

#include <openthread/backbone_router_ftd.h>

#include "agent/instance_params.hpp"
#include "agent/ncp_openthread.hpp"
#include "utils/system_utils.hpp"

namespace otbr {
namespace BackboneRouter {

/**
 * @addtogroup border-router-backbone
 *
 * @brief
 *   This module includes definition for the Multicast Routing management.
 *
 * @{
 */

/**
 * This class implements Multicast Routing management.
 *
 */
class MulticastRoutingManager
{
public:
    /**
     * This constructor initializes a Multicast Routing manager instance.
     *
     */
    explicit MulticastRoutingManager()
        : mMulticastRouterSock(-1)
    {
    }

    /**
     * This method enables the Multicast Routing manager.
     *
     */
    void Enable(void);

    /**
     * This method disables the Multicast Routing manager.
     *
     */
    void Disable(void);

    /**
     * This method adds a multicast route.
     *
     * NOTE: Multicast routes are only effective when the Multicast Routing manager is enabled.
     *
     * @param[in] aAddress  The multicast address.
     *
     */
    void Add(const Ip6Address &aAddress);

    /**
     * This method removes a multicast route.
     *
     * @param[in] aAddress  The multicast address.
     *
     */
    void Remove(const Ip6Address &aAddress);

    /**
     * This method updates the fd_set and timeout for mainloop.
     *
     * @param[inout]    aReadFdSet      A reference to fd_set for polling read.
     * @param[inout]    aWriteFdSet     A reference to fd_set for polling read.
     * @param[inout]    aErrorFdSet     A reference to fd_set for polling error.
     * @param[inout]    aMaxFd          A reference to the current max fd in @p aReadFdSet and @p aWriteFdSet.
     * @param[inout]    aTimeout        A reference to the timeout.
     *
     */
    void UpdateFdSet(fd_set & aReadFdSet,
                     fd_set & aWriteFdSet,
                     fd_set & aErrorFdSet,
                     int &    aMaxFd,
                     timeval &aTimeout) const;

    /**
     * This method performs Multicast Routing processing.
     *
     * @param[in]   aReadFdSet   A reference to read file descriptors.
     * @param[in]   aWriteFdSet  A reference to write file descriptors.
     * @param[in]   aErrorFdSet  A reference to error file descriptors.
     *
     */
    void Process(const fd_set &aReadFdSet, const fd_set &aWriteFdSet, const fd_set &aErrorFdSet);

private:
    enum
    {
        kMulticastForwardingCacheExpireTimeout = 300, //< Expire timeout of Multicast Forwarding Cache (in seconds)
    };

    enum MifIndex : uint8_t
    {
        kMifIndexNone     = 0xff,
        kMifIndexThread   = 0,
        kMifIndexBackbone = 1,
    };

    class MulticastRoute
    {
        friend class MulticastRoutingManager;

    public:
        MulticastRoute(const Ip6Address &aSrcAddr, const Ip6Address &aGroupAddr)
            : mSrcAddr(aSrcAddr)
            , mGroupAddr(aGroupAddr)
        {
        }

        bool operator<(const MulticastRoute &aOther) const;

    private:
        Ip6Address mSrcAddr;
        Ip6Address mGroupAddr;
    };

    typedef std::chrono::steady_clock Clock;

    class MulticastRouteInfo
    {
        friend class MulticastRoutingManager;

    public:
        MulticastRouteInfo(MifIndex aIif, MifIndex aOif)
            : mValidPktCnt(0)
            , mOif(aOif)
            , mIif(aIif)
        {
            mLastUseTime = Clock::now();
        }

        MulticastRouteInfo() = default;

    private:
        std::chrono::time_point<Clock> mLastUseTime;
        unsigned long                  mValidPktCnt;
        MifIndex                       mOif;
        MifIndex                       mIif;
    };

    bool        IsEnabled(void) const { return mMulticastRouterSock >= 0; }
    otbrError   InitMulticastRouterSock(void);
    void        FinalizeMulticastRouterSock(void);
    void        ProcessMulticastRouterMessages(void);
    otbrError   AddMulticastForwardingCache(const Ip6Address &aSrcAddr, const Ip6Address &aGroupAddr, MifIndex aIif);
    void        RemoveInboundMulticastForwardingCache(const Ip6Address &aGroupAddr);
    void        UnblockInboundMulticastForwardingCache(const Ip6Address &aGroupAddr);
    void        ExpireMulticastForwardingCache(void);
    bool        UpdateMulticastRouteInfo(const MulticastRoute &route);
    const char *MifIndexToString(MifIndex aMif);
    void        DumpMulticastForwardingCache(void);

    std::set<Ip6Address>                         mListenerSet;
    int                                          mMulticastRouterSock;
    std::map<MulticastRoute, MulticastRouteInfo> mMulticastForwardingCache;
};

/**
 * @}
 */

} // namespace BackboneRouter
} // namespace otbr

#endif // BACKBONE_ROUTER_MULTICAST_ROUTING_HPP_

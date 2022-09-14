/*
 *  Command handler
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) 2017 EPAM Systems Inc.
 */

#ifndef SRC_MOCKBACKEND_HPP_
#define SRC_MOCKBACKEND_HPP_

#include <XenStoreMock.hpp>
#include <xen/be/Log.hpp>
#include <xen/be/Utils.hpp>

extern "C" {
#include <xen/io/xenbus.h>
#include <xenctrl.h>
}

class MockBackend {
public:
    MockBackend(domid_t beDomId, domid_t feDomId);

private:
    domid_t mBeDomId;
    domid_t mFeDomId;
    XenBackend::AsyncContext mAsync;
    XenBackend::Log mLog;

    std::string mVsndFePath;
    std::string mVsndBePath;

    void setupVsnd();

    void onWriteXenStore(const std::string& path, const std::string& value);

    void onVsndBeStateChanged(XenbusState state);
    void setVsndFeState(XenbusState state);
};

#endif /* SRC_MOCKBACKEND_HPP_ */

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

#include "MockBackend.hpp"

using std::bind;
using std::string;
using std::to_string;

using namespace std::placeholders;

MockBackend::MockBackend(domid_t beDomId, domid_t feDomId) : mBeDomId(beDomId), mFeDomId(feDomId), mLog("MockBackend")
{
    XenStoreMock::writeValue("domid", to_string(mBeDomId));

    XenStoreMock::setDomainPath(mFeDomId, "/local/domain/" + to_string(mFeDomId));
    XenStoreMock::setDomainPath(mBeDomId, "/local/domain/" + to_string(mBeDomId));

    setupVsnd();

    XenStoreMock::setWriteValueCbk(bind(&MockBackend::onWriteXenStore, this, _1, _2));

    LOG(mLog, DEBUG) << "Create";
}

void MockBackend::setupVsnd()
{
    mVsndFePath = XenStoreMock::getDomainPath(mFeDomId);
    mVsndFePath += "/device/vsnd/0";

    mVsndBePath = XenStoreMock::getDomainPath(mBeDomId);
    mVsndBePath += "/backend/vsnd/" + to_string(mFeDomId) + "/0";

    XenStoreMock::writeValue(mVsndBePath + "/frontend", mVsndFePath);

    XenStoreMock::writeValue(mVsndFePath + "/state", to_string(XenbusStateInitialising));
    XenStoreMock::writeValue(mVsndBePath + "/state", to_string(XenbusStateInitialising));

    XenStoreMock::writeValue(mVsndFePath + "/0/short-name", "ShortName");
    XenStoreMock::writeValue(mVsndFePath + "/0/0/unique-id", "alsa<hw:0;0>prop.media:navi");
    XenStoreMock::writeValue(mVsndFePath + "/0/0/type", "p");
}

void MockBackend::onWriteXenStore(const string& path, const string& value)
{
    string vsndStatePath = mVsndBePath + "/state";

    if (path == vsndStatePath) {
        onVsndBeStateChanged(static_cast<XenbusState>(stoi(value)));
    }
}

void MockBackend::onVsndBeStateChanged(XenbusState state)
{
    switch (state) {
        case XenbusStateInitialising:
            mAsync.call(bind(&MockBackend::setVsndFeState, this, XenbusStateInitialising));
            break;
        case XenbusStateInitWait:
            mAsync.call(bind(&MockBackend::setVsndFeState, this, XenbusStateInitialised));
            break;
        default:
            break;
    }
}

void MockBackend::setVsndFeState(XenbusState state)
{
    LOG(mLog, DEBUG) << "Set vsnd FE state: " << state;

    if (state == XenbusStateInitialised) {
        XenStoreMock::writeValue(mVsndFePath + "/0/0/event-channel", "1");
        XenStoreMock::writeValue(mVsndFePath + "/0/0/ring-ref", "100");

        XenStoreMock::writeValue(mVsndFePath + "/0/0/evt-event-channel", "1");
        XenStoreMock::writeValue(mVsndFePath + "/0/0/evt-ring-ref", "100");
    }

    XenStoreMock::writeValue(mVsndFePath + "/state", to_string(state));
}

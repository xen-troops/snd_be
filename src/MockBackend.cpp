/*
 * MockBackend.cpp
 *
 *  Created on: Nov 8, 2017
 *      Author: al1
 */

#include "MockBackend.hpp"

using std::bind;
using std::string;
using std::to_string;

using namespace std::placeholders;

MockBackend::MockBackend(domid_t beDomId, domid_t feDomId) :
	mBeDomId(beDomId),
	mFeDomId(feDomId),
	mLog("MockBackend")
{
	XenStoreMock::writeValue("domid", to_string(mBeDomId));

	XenStoreMock::setDomainPath(mFeDomId, "/local/domain/" + to_string(mFeDomId));
	XenStoreMock::setDomainPath(mBeDomId, "/local/domain/" + to_string(mBeDomId));

	setupVsnd();

	XenStoreMock::setWriteValueCbk(bind(&MockBackend::onWriteXenStore, this,
									_1, _2));

	LOG(mLog, DEBUG) << "Create";
}

void MockBackend::setupVsnd()
{
	mVsndFePath = XenStoreMock::getDomainPath(mFeDomId);
	mVsndFePath += "/device/vsnd/0";

	mVsndBePath = XenStoreMock::getDomainPath(mBeDomId);
	mVsndBePath += "/backend/vsnd/" + to_string(mFeDomId) + "/0";

	XenStoreMock::writeValue(mVsndBePath + "/frontend", mVsndFePath);

	XenStoreMock::writeValue(mVsndFePath + "/state",
						 to_string(XenbusStateInitialising));
	XenStoreMock::writeValue(mVsndBePath + "/state",
						 to_string(XenbusStateInitialising));

	XenStoreMock::writeValue(mVsndFePath + "/0/short-name", "ShortName");
	XenStoreMock::writeValue(mVsndFePath + "/0/0/unique-id", "alsa<hw:0;0>prop.media:navi");
	XenStoreMock::writeValue(mVsndFePath + "/0/0/type", "p");
}

void MockBackend::onWriteXenStore(const string& path, const string& value)
{
	string vsndStatePath = mVsndBePath + "/state";

	if (path == vsndStatePath)
	{
		onVsndBeStateChanged(static_cast<XenbusState>(stoi(value)));
	}
}

void MockBackend::onVsndBeStateChanged(XenbusState state)
{
	switch(state)
	{
	case XenbusStateInitialising:
		mAsync.call(bind(&MockBackend::setVsndFeState, this,
					XenbusStateInitialising));
		break;
	case XenbusStateInitWait:
		mAsync.call(bind(&MockBackend::setVsndFeState, this,
					XenbusStateInitialised));
		break;
	default:
		break;
	}
}

void MockBackend::setVsndFeState(XenbusState state)
{
	LOG(mLog, DEBUG) << "Set vsnd FE state: " << state;

	if (state == XenbusStateInitialised)
	{
		XenStoreMock::writeValue(mVsndFePath + "/0/0/event-channel", "1");
		XenStoreMock::writeValue(mVsndFePath + "/0/0/ring-ref", "100");

		XenStoreMock::writeValue(mVsndFePath + "/0/0/evt-event-channel", "1");
		XenStoreMock::writeValue(mVsndFePath + "/0/0/evt-ring-ref", "100");
	}

	XenStoreMock::writeValue(mVsndFePath + "/state", to_string(state));
}


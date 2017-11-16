/*
 * MockBackend.hpp
 *
 *  Created on: Nov 8, 2017
 *      Author: al1
 */

#ifndef SRC_MOCKBACKEND_HPP_
#define SRC_MOCKBACKEND_HPP_

#include <XenStoreMock.hpp>

#include <xen/be/Log.hpp>
#include <xen/be/Utils.hpp>

extern "C" {
#include <xenctrl.h>
#include <xen/io/xenbus.h>
}

class MockBackend
{
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

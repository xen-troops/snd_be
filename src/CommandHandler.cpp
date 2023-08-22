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
 * Copyright (C) 2016 EPAM Systems Inc.
 */

#include "CommandHandler.hpp"

#include <sys/mman.h>

#include <errno.h>

#include <xen/be/Exception.hpp>

#ifdef WITH_ALSA
#include "AlsaPcm.hpp"
#endif

#ifdef WITH_PULSE
#include "PulsePcm.hpp"
#endif

#ifdef WITH_PIPEWIRE
#include "PipeWirePcm.hpp"
#endif

using std::bind;
using std::min;
using std::out_of_range;
using std::vector;
using std::unordered_map;

using namespace std::placeholders;

using XenBackend::XenGnttabBuffer;

using SoundItf::PcmDevicePtr;
using SoundItf::PcmParams;

unordered_map<int, CommandHandler::CommandFn> CommandHandler::sCmdTable =
{
	{XENSND_OP_OPEN,					&CommandHandler::open},
	{XENSND_OP_CLOSE,					&CommandHandler::close},
	{XENSND_OP_READ,					&CommandHandler::read},
	{XENSND_OP_WRITE,					&CommandHandler::write},
	{XENSND_OP_TRIGGER,					&CommandHandler::trigger},
	{XENSND_OP_HW_PARAM_QUERY,			&CommandHandler::queryHwParam},
};

/*******************************************************************************
 * CommandHandler
 ******************************************************************************/

CommandHandler::CommandHandler(PcmDevicePtr pcmDevice,
							   EventRingBufferPtr eventRingBuffer,
							   domid_t domId) :
	mPcmDevice(pcmDevice),
	mDomId(domId),
	mEventRingBuffer(eventRingBuffer),
	mEventId(0),
	mLog("CommandHandler")
{
	pcmDevice->setProgressCbk(bind(&CommandHandler::progressCbk, this, _1));

	LOG(mLog, DEBUG) << "Create command handler, dom: " << mDomId;
}

CommandHandler::~CommandHandler()
{
	LOG(mLog, DEBUG) << "Delete command handler, dom: " << mDomId;
}

/*******************************************************************************
 * Public
 ******************************************************************************/

int CommandHandler::processCommand(const xensnd_req& req, xensnd_resp& rsp)
{
	int status = 0;

	try
	{
		(this->*sCmdTable.at(req.operation))(req, rsp);
	}
	catch(const XenBackend::Exception& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = -e.getErrno();

		if (status >= 0)
		{
			DLOG(mLog, WARNING) << "Positive error code: "
								<< static_cast<signed int>(status);

			status = -EINVAL;
		}
	}
	catch(const std::out_of_range& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = -EINVAL;
	}
	catch(const std::exception& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = -EIO;
	}

	DLOG(mLog, DEBUG) << "Return status: [" << status << "]";

	return status;
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void CommandHandler::progressCbk(uint64_t frame)
{
	xensnd_evt event = { .id = mEventId++, .type = XENSND_EVT_CUR_POS };

	event.op.cur_pos.position = frame;

	mEventRingBuffer->sendEvent(event);
}

void CommandHandler::open(const xensnd_req& req, xensnd_resp& rsp)
{
	DLOG(mLog, DEBUG) << "Handle command [OPEN]";

	const xensnd_open_req& openReq = req.op.open;

	vector<grant_ref_t> refs;

	getBufferRefs(openReq.gref_directory, openReq.buffer_sz, refs);

	mBuffer.reset(new XenGnttabBuffer(mDomId, refs.data(), refs.size(),
									  PROT_READ | PROT_WRITE));

	mPcmDevice->open( {openReq.pcm_rate, openReq.pcm_format,
					   openReq.pcm_channels, openReq.buffer_sz,
					   openReq.period_sz } );
}

void CommandHandler::close(const xensnd_req& req, xensnd_resp& rsp)
{
	DLOG(mLog, DEBUG) << "Handle command [CLOSE]";

	mBuffer.reset();

	mPcmDevice->close();
}

void CommandHandler::read(const xensnd_req& req, xensnd_resp& rsp)
{
	DLOG(mLog, DEBUG) << "Handle command [READ]";

	const xensnd_rw_req& readReq = req.op.rw;

	mPcmDevice->read(&(static_cast<uint8_t*>(mBuffer->get())[readReq.offset]),
					 readReq.length);
}

void CommandHandler::write(const xensnd_req& req, xensnd_resp& rsp)
{
	DLOG(mLog, DEBUG) << "Handle command [WRITE]";

	const xensnd_rw_req& writeReq = req.op.rw;

	mPcmDevice->write(&(static_cast<uint8_t*>(mBuffer->get())[writeReq.offset]),
					  writeReq.length);
}

void CommandHandler::trigger(const xensnd_req& req, xensnd_resp& rsp)
{
	const xensnd_trigger_req& triggerReq = req.op.trigger;

	switch(triggerReq.type)
	{
	case XENSND_OP_TRIGGER_START:
		DLOG(mLog, DEBUG) << "Handle command [TRIGGER][START]";
		mPcmDevice->start();
		break;
	case XENSND_OP_TRIGGER_PAUSE:
		DLOG(mLog, DEBUG) << "Handle command [TRIGGER][PAUSE]";
		mPcmDevice->pause();
		break;
	case XENSND_OP_TRIGGER_STOP:
		DLOG(mLog, DEBUG) << "Handle command [TRIGGER][STOP]";
		mPcmDevice->stop();
		break;
	case XENSND_OP_TRIGGER_RESUME:
		DLOG(mLog, DEBUG) << "Handle command [TRIGGER][RESUME]";
		mPcmDevice->resume();
		break;
	default:
		throw XenBackend::Exception("Unknown trigger type", -EINVAL);
	}
}

void CommandHandler::queryHwParam(const xensnd_req& req, xensnd_resp& rsp)
{
	const xensnd_query_hw_param& queryHwParamReq = req.op.hw_param;
	xensnd_query_hw_param& queryHwParamResp = rsp.resp.hw_param;
	SoundItf::PcmParamRanges sndReq;
	SoundItf::PcmParamRanges sndResp;

	DLOG(mLog, DEBUG) << "Handle command [QUERY_HW_PARAM]";

	sndReq.formats = queryHwParamReq.formats;

	sndReq.rates.min = queryHwParamReq.rates.min;
	sndReq.rates.max = queryHwParamReq.rates.max;

	sndReq.channels.min = queryHwParamReq.channels.min;
	sndReq.channels.max = queryHwParamReq.channels.max;

	sndReq.buffer.min = queryHwParamReq.buffer.min;
	sndReq.buffer.max = queryHwParamReq.buffer.max;

	sndReq.period.min = queryHwParamReq.period.min;
	sndReq.period.max = queryHwParamReq.period.max;

	mPcmDevice->queryHwRanges(sndReq, sndResp);

	queryHwParamResp.formats = sndResp.formats;

	queryHwParamResp.rates.min = sndResp.rates.min;
	queryHwParamResp.rates.max = sndResp.rates.max;

	queryHwParamResp.channels.min = sndResp.channels.min;
	queryHwParamResp.channels.max = sndResp.channels.max;

	queryHwParamResp.buffer.min = sndResp.buffer.min;
	queryHwParamResp.buffer.max = sndResp.buffer.max;

	queryHwParamResp.period.min = sndResp.period.min;
	queryHwParamResp.period.max = sndResp.period.max;
}

void CommandHandler::getBufferRefs(grant_ref_t startDirectory, uint32_t size,
								   vector<grant_ref_t>& refs)
{
	refs.clear();

	size_t requestedNumGrefs = (size + XC_PAGE_SIZE - 1) / XC_PAGE_SIZE;

	DLOG(mLog, DEBUG) << "Get buffer refs, directory: " << startDirectory
					  << ", size: " << size
					  << ", in grefs: " << requestedNumGrefs;


	while(startDirectory != 0)
	{

		XenGnttabBuffer pageBuffer(mDomId, startDirectory);

		xensnd_page_directory* pageDirectory =
				static_cast<xensnd_page_directory*>(pageBuffer.get());

		size_t numGrefs = min(requestedNumGrefs, (XC_PAGE_SIZE -
							  offsetof(xensnd_page_directory, gref)) /
							  sizeof(uint32_t));

		DLOG(mLog, DEBUG) << "Gref address: " << pageDirectory->gref
						  << ", numGrefs " << numGrefs;

		refs.insert(refs.end(), pageDirectory->gref,
					pageDirectory->gref + numGrefs);

		requestedNumGrefs -= numGrefs;

		startDirectory = pageDirectory->gref_dir_next_page;
	}

	DLOG(mLog, DEBUG) << "Get buffer refs, num refs: " << refs.size();
}

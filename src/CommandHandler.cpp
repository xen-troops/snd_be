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

#include <xen/errno.h>

#include "AlsaPcm.hpp"
#include "PulsePcm.hpp"

using std::min;
using std::out_of_range;
using std::vector;
using std::unordered_map;

using XenBackend::XenException;
using XenBackend::XenGnttabBuffer;

using SoundItf::PcmParams;
using SoundItf::PcmType;
using SoundItf::SoundException;
using SoundItf::StreamType;

unordered_map<int, CommandHandler::CommandFn> CommandHandler::sCmdTable =
{
	{XENSND_OP_OPEN,	&CommandHandler::open},
	{XENSND_OP_CLOSE,	&CommandHandler::close},
	{XENSND_OP_READ,	&CommandHandler::read},
	{XENSND_OP_WRITE,	&CommandHandler::write}
};

/*******************************************************************************
 * CommandHandler
 ******************************************************************************/

CommandHandler::CommandHandler(PcmType pcmType, StreamType type, int domId) :
	mDomId(domId),
	mLog("CommandHandler")
{
	LOG(mLog, DEBUG) << "Create command handler, dom: " << mDomId;

	if (pcmType == PcmType::ALSA)
	{
		mPcmDevice.reset(new Alsa::AlsaPcm(type));
	}

	if (pcmType == PcmType::PULSE)
	{
		mPcmDevice.reset(new Pulse::PulsePcm(type, ""));
	}

	if (!mPcmDevice)
	{
		throw SoundException("Invalid PCM type", XEN_EINVAL);
	}
}

CommandHandler::~CommandHandler()
{
	LOG(mLog, DEBUG) << "Delete command handler, dom: " << mDomId;
}

/*******************************************************************************
 * Public
 ******************************************************************************/

int CommandHandler::processCommand(const xensnd_req& req)
{
	int status = 0;

	try
	{
		(this->*sCmdTable.at(req.operation))(req);
	}
	catch(const out_of_range& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = XEN_EINVAL;
	}
	catch(const SoundException& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = e.getErrno();
	}

	DLOG(mLog, DEBUG) << "Return status: [" << status << "]";

	return status;
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void CommandHandler::open(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Handle command [OPEN]";

	const xensnd_open_req& openReq = req.op.open;

	vector<grant_ref_t> refs;

	getBufferRefs(openReq.gref_directory, openReq.buffer_sz, refs);

	mBuffer.reset(new XenGnttabBuffer(mDomId, refs.data(), refs.size(),
									  PROT_READ | PROT_WRITE));

	mPcmDevice->open(PcmParams(openReq.pcm_rate, openReq.pcm_format,
							   openReq.pcm_channels));
}

void CommandHandler::close(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Handle command [CLOSE]";

	mBuffer.reset();

	mPcmDevice->close();
}

void CommandHandler::read(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Handle command [READ]";

	const xensnd_rw_req& readReq = req.op.rw;

	mPcmDevice->read(&(static_cast<uint8_t*>(mBuffer->get())[readReq.offset]),
					 readReq.length);
}

void CommandHandler::write(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Handle command [WRITE]";

	const xensnd_rw_req& writeReq = req.op.rw;

	mPcmDevice->write(&(static_cast<uint8_t*>(mBuffer->get())[writeReq.offset]),
					  writeReq.length);
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

		DLOG(mLog, ERROR) << "Gref address: " << pageDirectory->gref
						  << ", numGrefs " << numGrefs;

		refs.insert(refs.end(), pageDirectory->gref,
					pageDirectory->gref + numGrefs);

		requestedNumGrefs -= numGrefs;

		startDirectory = pageDirectory->gref_dir_next_page;
	}

	DLOG(mLog, DEBUG) << "Get buffer refs, num refs: " << refs.size();
}

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

using std::out_of_range;
using std::vector;
using std::unordered_map;

using XenBackend::XenException;
using XenBackend::XenGnttabBuffer;

using Alsa::AlsaPcmException;
using Alsa::AlsaPcmParams;

CommandHandler::PcmFormat CommandHandler::sPcmFormat[] =
{
	{XENSND_PCM_FORMAT_U8,                 SND_PCM_FORMAT_U8 },
	{XENSND_PCM_FORMAT_S8,                 SND_PCM_FORMAT_S8 },
	{XENSND_PCM_FORMAT_U16_LE,             SND_PCM_FORMAT_U16_LE },
	{XENSND_PCM_FORMAT_U16_BE,             SND_PCM_FORMAT_U16_BE },
	{XENSND_PCM_FORMAT_S16_LE,             SND_PCM_FORMAT_S16_LE },
	{XENSND_PCM_FORMAT_S16_BE,             SND_PCM_FORMAT_S16_BE },
	{XENSND_PCM_FORMAT_U24_LE,             SND_PCM_FORMAT_U24_LE },
	{XENSND_PCM_FORMAT_U24_BE,             SND_PCM_FORMAT_U24_BE },
	{XENSND_PCM_FORMAT_S24_LE,             SND_PCM_FORMAT_S24_LE },
	{XENSND_PCM_FORMAT_S24_BE,             SND_PCM_FORMAT_S24_BE },
	{XENSND_PCM_FORMAT_U32_LE,             SND_PCM_FORMAT_U32_LE },
	{XENSND_PCM_FORMAT_U32_BE,             SND_PCM_FORMAT_U32_BE },
	{XENSND_PCM_FORMAT_S32_LE,             SND_PCM_FORMAT_S32_LE },
	{XENSND_PCM_FORMAT_S32_BE,             SND_PCM_FORMAT_S32_BE },
	{XENSND_PCM_FORMAT_A_LAW,              SND_PCM_FORMAT_A_LAW },
	{XENSND_PCM_FORMAT_MU_LAW,             SND_PCM_FORMAT_MU_LAW },
	{XENSND_PCM_FORMAT_F32_LE,             SND_PCM_FORMAT_FLOAT_LE },
	{XENSND_PCM_FORMAT_F32_BE,             SND_PCM_FORMAT_FLOAT_BE },
	{XENSND_PCM_FORMAT_F64_LE,             SND_PCM_FORMAT_FLOAT64_LE },
	{XENSND_PCM_FORMAT_F64_BE,             SND_PCM_FORMAT_FLOAT64_BE },
	{XENSND_PCM_FORMAT_IEC958_SUBFRAME_LE, SND_PCM_FORMAT_IEC958_SUBFRAME_LE },
	{XENSND_PCM_FORMAT_IEC958_SUBFRAME_BE, SND_PCM_FORMAT_IEC958_SUBFRAME_BE },
	{XENSND_PCM_FORMAT_IMA_ADPCM,          SND_PCM_FORMAT_IMA_ADPCM },
	{XENSND_PCM_FORMAT_MPEG,               SND_PCM_FORMAT_MPEG },
	{XENSND_PCM_FORMAT_GSM,                SND_PCM_FORMAT_GSM },
	{XENSND_PCM_FORMAT_SPECIAL,            SND_PCM_FORMAT_SPECIAL },
};

unordered_map<int, CommandHandler::CommandFn> CommandHandler::sCmdTable =
{
	{XENSND_OP_OPEN,	&CommandHandler::open},
	{XENSND_OP_CLOSE,	&CommandHandler::close},
	{XENSND_OP_READ,	&CommandHandler::read},
	{XENSND_OP_WRITE,	&CommandHandler::write}
};

/***************************************************************************//**
 * CommandHandler
 ******************************************************************************/

CommandHandler::CommandHandler(Alsa::StreamType type, int domId) :
	mDomId(domId),
	mAlsaPcm(type),
	mLog("CommandHandler")
{
	LOG(mLog, DEBUG) << "Create command handler, dom: " << mDomId;
}

CommandHandler::~CommandHandler()
{
	LOG(mLog, DEBUG) << "Delete command handler, dom: " << mDomId;
}

/***************************************************************************//**
 * Public
 ******************************************************************************/

uint8_t CommandHandler::processCommand(const xensnd_req& req)
{
	uint8_t status = XENSND_RSP_OKAY;

	try
	{
		(this->*sCmdTable.at(req.u.data.operation))(req);
	}
	catch(const out_of_range& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = XENSND_RSP_ERROR;
	}
	catch(const AlsaPcmException& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = XENSND_RSP_ERROR;
	}

	DLOG(mLog, DEBUG) << "Return status: [" << static_cast<int>(status) << "]";

	return status;
}

/***************************************************************************//**
 * Private
 ******************************************************************************/

void CommandHandler::open(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Handle command [OPEN]";

	const xensnd_open_req& openReq = req.u.data.op.open;

	vector<grant_ref_t> refs;

	getBufferRefs(openReq.gref_directory_start, refs);

	mBuffer.reset(new XenGnttabBuffer(mDomId, refs.data(), refs.size(),
									  PROT_READ | PROT_WRITE));

	mAlsaPcm.open(AlsaPcmParams(convertPcmFormat(openReq.pcm_format),
								openReq.pcm_rate, openReq.pcm_channels));
}

void CommandHandler::close(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Handle command [CLOSE]";

	mBuffer.reset();

	mAlsaPcm.close();
}

void CommandHandler::read(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Handle command [READ]";

	const xensnd_read_req& readReq = req.u.data.op.read;

	mAlsaPcm.read(&(static_cast<uint8_t*>(mBuffer->get())[readReq.offset]),
				  readReq.len);
}

void CommandHandler::write(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Handle command [WRITE]";

	const xensnd_write_req& writeReq = req.u.data.op.write;

	mAlsaPcm.write(&(static_cast<uint8_t*>(mBuffer->get())[writeReq.offset]),
				   writeReq.len);
}

void CommandHandler::getBufferRefs(grant_ref_t startDirectory,
								   vector<grant_ref_t>& refs)
{
	refs.clear();

	do
	{

		XenGnttabBuffer pageBuffer(mDomId, startDirectory,
								   PROT_READ | PROT_WRITE);
		xensnd_page_directory* pageDirectory =
				static_cast<xensnd_page_directory*>(pageBuffer.get());

		DLOG(mLog, DEBUG) << "Get buffer refs, directory: " << startDirectory
						  << ", num refs: " << pageDirectory->num_grefs;

		refs.insert(refs.end(), pageDirectory->gref,
					pageDirectory->gref + pageDirectory->num_grefs);

		startDirectory = pageDirectory->gref_dir_next_page;

	}
	while(startDirectory != 0);

	DLOG(mLog, DEBUG) << "Get buffer refs, num refs: " << refs.size();
}

snd_pcm_format_t CommandHandler::convertPcmFormat(uint8_t format)
{
	for (auto value : sPcmFormat)
	{
		if (value.sndif == format)
		{
			return value.alsa;
		}
	}

	throw AlsaPcmException("Can't convert format");
}

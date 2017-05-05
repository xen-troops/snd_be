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

#ifndef SRC_COMMANDHANDLER_HPP_
#define SRC_COMMANDHANDLER_HPP_

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <xen/be/XenGnttab.hpp>
#include <xen/be/Log.hpp>

#include <xen/io/sndif.h>

#include "AlsaPcm.hpp"

/**
 * Handles commands received from the frontend.
 * @ingroup snd_be
 */
class CommandHandler
{
public:
	/**
	 * @param type  Alsa stream type
	 * @param domId domain id
	 */
	CommandHandler(SoundItf::StreamType type, int domId);
	~CommandHandler();

	/**
	 * Processes commands received from the frontend.
	 * @param req
	 * @return status
	 */
	int processCommand(const xensnd_req& req);

private:

	typedef void(CommandHandler::*CommandFn)(const xensnd_req& req);

	static std::unordered_map<int, CommandFn> sCmdTable;

	int mDomId;
	std::unique_ptr<XenBackend::XenGnttabBuffer> mBuffer;

	Alsa::AlsaPcm mAlsaPcm;

	XenBackend::Log mLog;

	void open(const xensnd_req& req);
	void close(const xensnd_req& req);
	void read(const xensnd_req& req);
	void write(const xensnd_req& req);

	void getBufferRefs(grant_ref_t startDirectory, uint32_t size, std::vector<grant_ref_t>& refs);
};

#endif /* SRC_COMMANDHANDLER_HPP_ */

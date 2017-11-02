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

#include <xen/be/Log.hpp>
#include <xen/be/RingBufferBase.hpp>
#include <xen/be/XenGnttab.hpp>

#include <xen/io/sndif.h>

#include "SoundItf.hpp"

/***************************************************************************//**
 * Ring buffer used to send events to the frontend.
 * @ingroup snd_be
 ******************************************************************************/
class EventRingBuffer : public XenBackend::RingBufferOutBase<
		xensnd_event_page, xensnd_evt>
{
public:
	/**
	 * @param domId     frontend domain id
	 * @param port      event channel port number
	 * @param ref       grant table reference
	 * @param offset    start of the ring buffer inside the page
	 * @param size      size of the ring buffer
	 */
	EventRingBuffer(domid_t domId, evtchn_port_t port,
					grant_ref_t ref, int offset, size_t size) :
		RingBufferOutBase<xensnd_event_page, xensnd_evt>(domId, port, ref,
														 offset, size) {}
};

typedef std::shared_ptr<EventRingBuffer> EventRingBufferPtr;


/***************************************************************************//**
 * Handles commands received from the frontend.
 * @ingroup snd_be
 ******************************************************************************/
class CommandHandler
{
public:

	/**
	 * @param type  Alsa stream type
	 * @param domId domain id
	 */
	CommandHandler(std::shared_ptr<SoundItf::PcmDevice> pcmDevice,
				   EventRingBufferPtr eventRingBuffer, domid_t domId);
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

	std::shared_ptr<SoundItf::PcmDevice> mPcmDevice;
	domid_t mDomId;
	EventRingBufferPtr mEventRingBuffer;
	std::unique_ptr<XenBackend::XenGnttabBuffer> mBuffer;
	uint16_t mEventId;

	XenBackend::Log mLog;

	void progressCbk(uint64_t bytes);

	void open(const xensnd_req& req);
	void close(const xensnd_req& req);
	void read(const xensnd_req& req);
	void write(const xensnd_req& req);
	void trigger(const xensnd_req& req);

	void getBufferRefs(grant_ref_t startDirectory, uint32_t size, std::vector<grant_ref_t>& refs);
};

#endif /* SRC_COMMANDHANDLER_HPP_ */

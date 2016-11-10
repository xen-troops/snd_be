/*
 *  Sound backend
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

#ifndef INCLUDE_SNDBACKEND_HPP_
#define INCLUDE_SNDBACKEND_HPP_

#include <xen/be/BackendBase.hpp>
#include <xen/be/FrontendHandlerBase.hpp>
#include <xen/be/RingBufferBase.hpp>
#include <xen/be/Log.hpp>

#include "CommandHandler.hpp"

extern "C" {
#include "sndif_linux.h"
}

/***************************************************************************//**
 * @defgroup snd_be
 * Backend related classes.
 ******************************************************************************/

class SndFrontendHandler;

/***************************************************************************//**
 * Ring buffer used for the audio stream.
 * @ingroup snd_be
 ******************************************************************************/
class StreamRingBuffer : public XenBackend::RingBufferInBase<
											xen_sndif_back_ring,
											xen_sndif_sring,
											xensnd_req,
											xensnd_resp>
{
public:
	/**
	 * @param id    stream id
	 * @param type  stream type
	 * @param domId frontend domain id
	 * @param port  event channel port number
	 * @param ref   grant table reference
	 */
	StreamRingBuffer(int id, Alsa::StreamType type, int domId,
					 int port, int ref);

private:
	int mId;
	CommandHandler mCommandHandler;
	XenBackend::Log mLog;

	void processRequest(const xensnd_req& req);
};

/***************************************************************************//**
 * Sound frontend handler.
 * @ingroup snd_be
 ******************************************************************************/
class SndFrontendHandler : public XenBackend::FrontendHandlerBase
{
public:

	/**
	 * @param domId   frontend domain id
	 * @param backend backend instance
	 * @param id      frontend instance id
	 */
	SndFrontendHandler(int domId, XenBackend::BackendBase& backend, int id) :
		FrontendHandlerBase(domId, backend, id),
		mLog("AlsaFrontend") {}

protected:

	/**
	 * Is called on connected state when ring buffers binding is required.
	 */
	void onBind();

private:

	XenBackend::Log mLog;

	void createStream(int id, Alsa::StreamType type,
					  const std::string& streamPath);
	void processCard(const std::string& cardPath);
	void processDevice(const std::string& devPath);
	void processStream(const std::string& streamPath);
};

/***************************************************************************//**
 * Sound backend class.
 * @ingroup snd_be
 ******************************************************************************/
class SndBackend : public XenBackend::BackendBase
{
	using XenBackend::BackendBase::BackendBase;

protected:

	/**
	 * Is called when new sound frontend appears.
	 * @param domId domain id
	 * @param id    instance id
	 */
	void onNewFrontend(int domId, int id);
};

#endif /* INCLUDE_SNDBACKEND_HPP_ */

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

#ifndef SRC_SNDBACKEND_HPP_
#define SRC_SNDBACKEND_HPP_

#include <xen/be/BackendBase.hpp>
#include <xen/be/FrontendHandlerBase.hpp>
#include <xen/be/RingBufferBase.hpp>
#include <xen/be/Log.hpp>

#include "CommandHandler.hpp"

/***************************************************************************//**
 * @defgroup snd_be
 * Backend related classes.
 ******************************************************************************/

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
	StreamRingBuffer(int id, SoundItf::PcmType pcmType,
					 SoundItf::StreamType streamType, domid_t domId,
					 evtchn_port_t port, grant_ref_t ref);

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
	SndFrontendHandler(SoundItf::PcmType pcmType, const std::string devName,
					   domid_t beDomId, domid_t feDomId, uint16_t devId) :
		FrontendHandlerBase("SndFrontend", devName, beDomId, feDomId, devId),
		mPcmType(pcmType),
		mLog("SndFrontend") {}

protected:

	/**
	 * Is called on connected state when ring buffers binding is required.
	 */
	void onBind() override;

	/**
	 * Is called on connected state when ring buffers releases are required.
	 */
	void onClosing() override;

private:

	SoundItf::PcmType mPcmType;

	XenBackend::Log mLog;

	void createStream(int id, SoundItf::StreamType type,
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
public:

	SndBackend(SoundItf::PcmType pcmType, const std::string& deviceName,
			   domid_t domId) : BackendBase("SndBackend", deviceName, domId),
		mPcmType(pcmType) {}

protected:

	/**
	 * Is called when new sound frontend appears.
	 * @param domId domain id
	 * @param devId device id
	 */
	void onNewFrontend(domid_t domId, uint16_t devId) override;

private:

	SoundItf::PcmType mPcmType;
};

#endif /* SRC_SNDBACKEND_HPP_ */

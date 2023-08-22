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

#ifdef WITH_ALSA
#include "AlsaPcm.hpp"
#endif

#ifdef WITH_PULSE
#include "PulsePcm.hpp"
#endif

#ifdef WITH_PIPEWIRE
#include "PipeWirePcm.hpp"
#endif

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
	StreamRingBuffer(const std::string& id,
					 SoundItf::PcmDevicePtr pcmDevice,
					 EventRingBufferPtr eventRingBuffer,
					 domid_t domId, evtchn_port_t port, grant_ref_t ref);

private:
	std::string mId;
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
	 * @param devName device name
	 * @param domId frontend domain id
	 * @param devId   device id
	 */
	SndFrontendHandler(const std::string devName,
					   domid_t domId, uint16_t devId);

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

#ifdef WITH_PULSE
	Pulse::PulseMainloop mPulseMainloop;
#endif

#ifdef WITH_PIPEWIRE
	PipeWire::PipeWireMainloop mPipeWireMainloop;
#endif

	XenBackend::Log mLog;

	SoundItf::PcmDevicePtr createPcmDevice(SoundItf::StreamType type,
										   const std::string& id);
	void parseStreamId(const std::string& id,
					   std::string& pcmType, std::string& deviceName,
					   std::string& propName, std::string& propValue);
	std::string parsePcmType(std::string& input);
	std::string parseDeviceName(std::string& input);
	std::string parsePropName(std::string& input);
	std::string parsePropValue(std::string& input);

	void createStream(const std::string& id, SoundItf::StreamType type,
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

	SndBackend(const std::string& deviceName) :
		BackendBase("SndBackend", deviceName) {}

protected:

	/**
	 * Is called when new sound frontend appears.
	 * @param domId domain id
	 * @param devId device id
	 */
	void onNewFrontend(domid_t domId, uint16_t devId) override;
};

#endif /* SRC_SNDBACKEND_HPP_ */

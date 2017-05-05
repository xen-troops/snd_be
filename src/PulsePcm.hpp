/*
 *  PulseAudio pcm device wrapper
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

#ifndef SRC_PULSEPCM_HPP_
#define SRC_PULSEPCM_HPP_

#include <pulse/simple.h>

#include <xen/be/Log.hpp>

#include "SoundItf.hpp"

namespace Pulse {

/***************************************************************************//**
 * @defgroup pulse
 * PulseAudio related classes.
 ******************************************************************************/

/***************************************************************************//**
 * Provides PulseAudio pcm functionality.
 * @ingroup pulse
 ******************************************************************************/
class PulsePcm : public SoundItf::PcmDevice
{
public:
	/**
	 * @param type stream type
	 * @param name pcm device name
	 */
	PulsePcm(SoundItf::StreamType type, const std::string& streamName);
	~PulsePcm();

	/**
	 * Opens the pcm device.
	 * @param params pcm parameters
	 */
	void open(const SoundItf::PcmParams& params) override;

	/**
	 * Closes the pcm device.
	 */
	void close() override;

	/**
	 * Reads data from the pcm device.
	 * @param buffer buffer where to put data
	 * @param size   number of bytes to read
	 */
	void read(uint8_t* buffer, ssize_t size) override;

	/**
	 * Writes data to the pcm device.
	 * @param buffer buffer with data
	 * @param size   number of bytes to write
	 */
	void write(uint8_t* buffer, ssize_t size) override;

private:

	struct PcmFormat
	{
		uint8_t sndif;
		pa_sample_format_t pulse;
	};

	static PcmFormat sPcmFormat[];

	pa_simple *mSimple;
	SoundItf::StreamType mType;
	std::string mStreamName;
	XenBackend::Log mLog;

	pa_sample_format_t convertPcmFormat(uint8_t format);
};

}

#endif /* SRC_PULSEPCM_HPP_ */

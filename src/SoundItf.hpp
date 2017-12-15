/*
 *  Sound device interface
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

#ifndef SRC_SOUNDITF_HPP_
#define SRC_SOUNDITF_HPP_

#include <functional>
#include <memory>
#include <string>

#include <xen/be/Log.hpp>

namespace SoundItf {

/***************************************************************************//**
 * @defgroup sound
 * Sound interface related classes.
 ******************************************************************************/

/**
 * Specifies stream type
 * @ingroup sound
 */
enum class StreamType {PLAYBACK, CAPTURE};

/**
 * Specifies PCM device type
 * @ingroup sound
 */
enum class PcmType {ALSA, PULSE};

/**
 * Progress callback type
 * @ingroup sound
 */
typedef std::function<void(uint64_t bytes)> ProgressCbk;

/***************************************************************************//**
 * Describes pcm parameters.
 * @ingroup sound
 ******************************************************************************/
struct PcmParams
{
	uint32_t	rate;			//!< rate in Hz
	uint8_t		format;			//!< pcm format
	uint8_t		numChannels;	//!< number of channels
	uint32_t	bufferSize;		//!< buffer size
	uint32_t	periodSize;		//!< period size
};

/***************************************************************************//**
 * Provides sound functionality.
 * @ingroup sound
 ******************************************************************************/
class PcmDevice
{
public:

	virtual ~PcmDevice() {}

	/**
	 * Opens the device.
	 * @param params pcm parameters
	 */
	virtual void open(const PcmParams& params) = 0;

	/**
	 * Closes the device.
	 */
	virtual void close() = 0;

	/**
	 * Reads data from the device.
	 * @param buffer buffer where to put data
	 * @param size   number of bytes to read
	 */
	virtual void read(uint8_t* buffer, size_t size) = 0;

	/**
	 * Writes data to the device.
	 * @param buffer buffer with data
	 * @param size   number of bytes to write
	 */
	virtual void write(uint8_t* buffer, size_t size) = 0;

	/**
	 * Starts the pcm device.
	 */
	virtual void start() = 0;

	/**
	 * Stops the pcm device.
	 */
	virtual void stop() = 0;

	/**
	 * Pauses the pcm device.
	 */
	virtual void pause() = 0;

	/**
	 * Resumes the pcm device.
	 */
	virtual void resume() = 0;

	/**
	 * Sets progress callback.
	 * @param cbk callback
	 */
	virtual void setProgressCbk(ProgressCbk cbk) = 0;
};

typedef std::shared_ptr<PcmDevice> PcmDevicePtr;

}

#endif /* SRC_SOUNDITF_HPP_ */

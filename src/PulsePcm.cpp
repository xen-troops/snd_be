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

#include "PulsePcm.hpp"

#include <pulse/error.h>

#include <xen/errno.h>
#include <xen/io/sndif.h>

using std::string;
using std::to_string;

using SoundItf::PcmParams;
using SoundItf::SoundException;
using SoundItf::StreamType;

namespace Pulse {

/*******************************************************************************
 * PulsePcm
 ******************************************************************************/

PulsePcm::PulsePcm(StreamType type, const std::string& streamName) :
	mSimple(nullptr),
	mType(type),
	mStreamName(streamName),
	mLog("PulsePcm")
{
	LOG(mLog, DEBUG) << "Create pcm device: " << mStreamName;
}

PulsePcm::~PulsePcm()
{
	LOG(mLog, DEBUG) << "Delete pcm device: " << mStreamName;

	close();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

void PulsePcm::open(const PcmParams& params)
{
	DLOG(mLog, DEBUG) << "Open pcm device: " << mStreamName
					  << ", format: " << params.format
					  << ", rate: " << params.rate
					  << ", channels: " << params.numChannels;

	pa_stream_direction_t streamType = mType == StreamType::PLAYBACK ?
			PA_STREAM_PLAYBACK : PA_STREAM_RECORD;

	const pa_sample_spec ss =
	{
		.format = convertPcmFormat(params.format),
		.rate = params.rate,
		.channels = params.numChannels
	};

	int error = 0;

	mSimple = pa_simple_new(nullptr, "snd_be", streamType, nullptr,
							mStreamName.c_str(), &ss, nullptr,
							nullptr, &error);

	if (!mSimple)
	{
		throw SoundException(pa_strerror(error), error);
	}
}

void PulsePcm::close()
{
	DLOG(mLog, DEBUG) << "Close pcm device: " << mStreamName;

	if (mSimple)
	{
		pa_simple_drain(mSimple, nullptr);
		pa_simple_free(mSimple);
	}
}

void PulsePcm::read(uint8_t* buffer, ssize_t size)
{
	DLOG(mLog, DEBUG) << "Read from pcm device: " << mStreamName
					  << ", size: " << size;

	int error = 0;

	if (pa_simple_read(mSimple, buffer, size, &error) < 0)
	{
		throw SoundException(pa_strerror(error), error);
	}
}

void PulsePcm::write(uint8_t* buffer, ssize_t size)
{
	DLOG(mLog, DEBUG) << "Write to pcm device: " << mStreamName
					  << ", size: " << size;

	int error = 0;

	if (pa_simple_write(mSimple, buffer, size, &error) < 0)
	{
		throw SoundException(pa_strerror(error), error);
	}
}

/*******************************************************************************
 * Private
 ******************************************************************************/

PulsePcm::PcmFormat PulsePcm::sPcmFormat[] =
{
	{XENSND_PCM_FORMAT_U8,                 PA_SAMPLE_U8 },
	{XENSND_PCM_FORMAT_S16_LE,             PA_SAMPLE_S16LE },
	{XENSND_PCM_FORMAT_S16_BE,             PA_SAMPLE_S16BE },
	{XENSND_PCM_FORMAT_S24_LE,             PA_SAMPLE_S24LE },
	{XENSND_PCM_FORMAT_S24_BE,             PA_SAMPLE_S24BE },
	{XENSND_PCM_FORMAT_S32_LE,             PA_SAMPLE_S32LE },
	{XENSND_PCM_FORMAT_S32_BE,             PA_SAMPLE_S32BE },
	{XENSND_PCM_FORMAT_A_LAW,              PA_SAMPLE_ALAW },
	{XENSND_PCM_FORMAT_MU_LAW,             PA_SAMPLE_ULAW },
	{XENSND_PCM_FORMAT_F32_LE,             PA_SAMPLE_FLOAT32LE },
	{XENSND_PCM_FORMAT_F32_BE,             PA_SAMPLE_FLOAT32BE },
};

pa_sample_format_t PulsePcm::convertPcmFormat(uint8_t format)
{
	for (auto value : sPcmFormat)
	{
		if (value.sndif == format)
		{
			return value.pulse;
		}
	}

	throw SoundException("Can't convert format", XEN_EINVAL);
}

}

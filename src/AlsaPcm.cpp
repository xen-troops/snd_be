/*
 *  Alsa pcm device wrapper
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

#include "AlsaPcm.hpp"

#include <exception>

using std::exception;
using std::string;
using std::to_string;

namespace Alsa {

/***************************************************************************//**
 * AlsaPcm
 ******************************************************************************/

AlsaPcm::AlsaPcm(StreamType type, const std::string& name) :
	mHandle(nullptr),
	mName(name),
	mType(type),
	mLog("AlsaPcm")
{
	LOG(mLog, DEBUG) << "Create pcm device: " << mName;
}

AlsaPcm::~AlsaPcm()
{
	LOG(mLog, DEBUG) << "Delete pcm device: " << mName;

	close();
}

/***************************************************************************//**
 * Public
 ******************************************************************************/

void AlsaPcm::open(const AlsaPcmParams& params)
{
	snd_pcm_hw_params_t *hwParams = nullptr;

	try
	{
		DLOG(mLog, DEBUG) << "Open pcm device: " << mName
						  << ", format: " << params.format
						  << ", rate: " << params.rate
						  << ", channels: " << params.numChannels;


		snd_pcm_stream_t streamType = mType == StreamType::PLAYBACK ?
				SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE;

		int ret = 0;

		if (ret = snd_pcm_open(&mHandle, mName.c_str(), streamType, 0) < 0)
		{
			throw AlsaPcmException("Can't open audio device " + mName, -ret);
		}

		if (ret = snd_pcm_hw_params_malloc(&hwParams) < 0)
		{
			throw AlsaPcmException("Can't allocate hw params " + mName, -ret);
		}

		if (ret = snd_pcm_hw_params_any(mHandle, hwParams) < 0)
		{
			throw AlsaPcmException("Can't allocate hw params " + mName, -ret);
		}

		if (ret = snd_pcm_hw_params_set_access(mHandle, hwParams,
				SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
		{
			throw AlsaPcmException("Can't set access " + mName, -ret);
		}

		if (ret = snd_pcm_hw_params_set_format(mHandle, hwParams,
											   params.format) < 0)
		{
			throw AlsaPcmException("Can't set format " + mName, -ret);
		}

		unsigned int rate = params.rate;

		if (ret = snd_pcm_hw_params_set_rate_near(mHandle, hwParams,
												  &rate, 0) < 0)
		{
			throw AlsaPcmException("Can't set rate " + mName, -ret);
		}

		if (ret = snd_pcm_hw_params_set_channels(mHandle, hwParams,
												 params.numChannels) < 0)
		{
			throw AlsaPcmException("Can't set channels " + mName, -ret);
		}

		if (ret = snd_pcm_hw_params(mHandle, hwParams) < 0)
		{
			throw AlsaPcmException("Can't set hwParams " + mName, -ret);
		}

		if (ret = snd_pcm_prepare(mHandle) < 0)
		{
			throw AlsaPcmException(
					"Can't prepare audio interface for use", -ret);
		}
	}
	catch(const AlsaPcmException& e)
	{
		if (hwParams)
		{
			snd_pcm_hw_params_free(hwParams);

			close();
		}

		throw;
	}
}

void AlsaPcm::close()
{
	DLOG(mLog, DEBUG) << "Close pcm device: " << mName;

	if (mHandle)
	{
		snd_pcm_drain(mHandle);
		snd_pcm_close(mHandle);
	}

	mHandle = nullptr;
}

void AlsaPcm::read(uint8_t* buffer, ssize_t size)
{
	DLOG(mLog, DEBUG) << "Read from pcm device: " << mName
					  << ", size: " << size;

	auto numFrames = snd_pcm_bytes_to_frames(mHandle, size);

	while(numFrames > 0)
	{
		if (auto status = snd_pcm_readi(mHandle, buffer, numFrames))
		{
			if (status == -EPIPE)
			{
				LOG(mLog, WARNING) << "Device: " << mName
								   << ", message: " << snd_strerror(status);

				snd_pcm_prepare(mHandle);
			}
			else if (status < 0)
			{
				throw AlsaPcmException("Read from audio interface failed: " +
									   mName + ". Error: " +
									   snd_strerror(status), -status);
			}
			else
			{
				numFrames -= status;
				buffer = &buffer[snd_pcm_frames_to_bytes(mHandle, status)];
			}
		}
	}
}

void AlsaPcm::write(uint8_t* buffer, ssize_t size)
{
	DLOG(mLog, DEBUG) << "Write to pcm device: " << mName << ", size: " << size;

	auto numFrames = snd_pcm_bytes_to_frames(mHandle, size);

	while(numFrames > 0)
	{
		if (auto status = snd_pcm_writei(mHandle, buffer, numFrames))
		{
			if (status == -EPIPE)
			{
				LOG(mLog, WARNING) << "Device: " << mName
								   << ", message: " << snd_strerror(status);

				snd_pcm_prepare(mHandle);
			}
			else if (status < 0)
			{
				throw AlsaPcmException("Write to audio interface failed: " +
									   mName + ". Error: " +
									   snd_strerror(status), -status);
			}
			else
			{
				numFrames -= status;
				buffer = &buffer[snd_pcm_frames_to_bytes(mHandle, status)];
			}
		}
	}
}

}

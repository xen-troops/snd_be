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

#include <xen/io/sndif.h>

using std::lock_guard;
using std::string;
using std::to_string;

using SoundItf::PcmParams;
using SoundItf::SoundException;
using SoundItf::StreamType;

namespace Pulse {

void contextError(pa_context* context)
{
	int error = pa_context_errno(context);

	throw SoundException(string("Pulse error: ") + pa_strerror(error), -error);
}

/*******************************************************************************
 * PulseMainloop
 ******************************************************************************/

PulseMainloop::PulseMainloop(const string& name) :
	mMainloop(nullptr),
	mContext(nullptr),
	mMutex(nullptr),
	mLog("PulseMainloop")
{
	try
	{
		init(name);
	}
	catch(const SoundException& e)
	{
		release();

		throw;
	}
}

PulseMainloop::~PulseMainloop()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

PulsePcm* PulseMainloop::createStream(StreamType type, const string& name,
									  const string& propName,
									  const string& propValue,
									  const string& deviceName)
{
	return new PulsePcm(mMainloop, mContext, type, name,
						propName, propValue, deviceName);
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void PulseMainloop::sContextStateChanged(pa_context *context, void *data)
{
	static_cast<PulseMainloop*>(data)->contextStateChanged();
}

void PulseMainloop::contextStateChanged()
{
	switch (pa_context_get_state(mContext))
	{
		case PA_CONTEXT_READY:
		case PA_CONTEXT_TERMINATED:
		case PA_CONTEXT_FAILED:

			pa_threaded_mainloop_signal(mMainloop, 0);

			break;

		default:
			break;
	}
}

void PulseMainloop::waitContextReady()
{
	for (;;)
	{
		auto state = pa_context_get_state(mContext);

		if (state == PA_CONTEXT_READY)
		{
			break;
		}

		if (!PA_CONTEXT_IS_GOOD(state))
		{
			contextError(mContext);
		}

		pa_threaded_mainloop_wait(mMainloop);
	}

	LOG(mLog, DEBUG) << "Context is ready";
}

void PulseMainloop::init(const string& name)
{
	LOG(mLog, DEBUG) << "Init";

	mMainloop = pa_threaded_mainloop_new();

	if (!mMainloop)
	{
		throw SoundException("Can't create Pulse mainloop", -PA_ERR_UNKNOWN);
	}

	mMutex = PulseMutex(mMainloop);

	auto api = pa_threaded_mainloop_get_api(mMainloop);

	if (!api)
	{
		throw SoundException("Can't get Pulse API", -PA_ERR_UNKNOWN);
	}

	mContext = pa_context_new(api, name.c_str());

	if (!mContext)
	{
		throw SoundException("Can't create Pulse context", -PA_ERR_UNKNOWN);
	}

	pa_context_set_state_callback(mContext, sContextStateChanged, this);

	if (pa_context_connect(mContext, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
	{
		contextError(mContext);
	}

	lock_guard<PulseMutex> lock(mMutex);

	if (pa_threaded_mainloop_start(mMainloop) < 0)
	{
		throw SoundException("Can't start Pulse mainloop", -PA_ERR_UNKNOWN);
	}

	waitContextReady();
}

void PulseMainloop::release()
{
	if (mContext)
	{
		pa_context_disconnect(mContext);
		pa_context_unref(mContext);
	}

	if (mMainloop)
	{
		pa_threaded_mainloop_stop(mMainloop);
		pa_threaded_mainloop_free(mMainloop);
	}

	LOG(mLog, DEBUG) << "Release";
}

/*******************************************************************************
 * PulsePcm
 ******************************************************************************/

PulsePcm::PulsePcm(pa_threaded_mainloop* mainloop, pa_context* context,
				   StreamType type, const string& name,
				   const string& propName, const string& propValue,
				   const string& deviceName) :
	mMainloop(mainloop),
	mContext(context),
	mStream(nullptr),
	mSuccess(0),
	mMutex(mainloop),
	mType(type),
	mName(name),
	mPropName(propName),
	mPropValue(propValue),
	mDeviceName(deviceName),
	mReadData(nullptr),
	mReadIndex(0),
	mReadLength(0),
	mLog("PulsePcm")
{
	LOG(mLog, DEBUG) << "Create pcm device: " << mName;
}

PulsePcm::~PulsePcm()
{
	close();

	LOG(mLog, DEBUG) << "Delete pcm device: "<< mName;
}

/*******************************************************************************
 * Public
 ******************************************************************************/

void PulsePcm::open(const PcmParams& params)
{
	lock_guard<PulseMutex> lock(mMutex);

	DLOG(mLog, DEBUG) << "Open pcm device: " << mName
					  << ", format: " << static_cast<int>(params.format)
					  << ", rate: " << params.rate
					  << ", channels: " << static_cast<int>(params.numChannels);

	if (mStream)
	{
		throw SoundException("PCM device " + mName + " already opened",
							 -PA_ERR_EXIST);
	}

	pa_stream_direction_t streamType = mType == StreamType::PLAYBACK ?
			PA_STREAM_PLAYBACK : PA_STREAM_RECORD;

	const pa_sample_spec ss =
	{
		.format = convertPcmFormat(params.format),
		.rate = params.rate,
		.channels = params.numChannels
	};

	PulseProplist propList(mPropName.c_str(), mPropValue.c_str());

	mStream = pa_stream_new_with_proplist(mContext, mName.c_str(), &ss,
										  nullptr, &propList);

	if (!mStream)
	{
		throw SoundException("Can't open PCM device " + mName, -PA_ERR_UNKNOWN);
	}

	pa_stream_set_state_callback(mStream, sStreamStateChanged, this);

	const char* deviceName = nullptr;

	if (!mDeviceName.empty())
	{
		deviceName = mDeviceName.c_str();
	}

	int ret = 0;

	if (streamType == PA_STREAM_PLAYBACK)
	{
		pa_stream_set_write_callback(mStream, sStreamRequest, this);
		pa_stream_set_latency_update_callback(mStream, sLatencyUpdate, this);

		ret = pa_stream_connect_playback(mStream, deviceName, nullptr,
										 static_cast<pa_stream_flags_t>(
										 PA_STREAM_INTERPOLATE_TIMING |
										 PA_STREAM_ADJUST_LATENCY |
										 PA_STREAM_AUTO_TIMING_UPDATE),
										 nullptr, nullptr);
	}
	else
	{
		pa_stream_set_read_callback(mStream, sStreamRequest, this);

		ret = pa_stream_connect_record(mStream, deviceName, nullptr,
									   static_cast<pa_stream_flags_t>(
									   PA_STREAM_INTERPOLATE_TIMING |
									   PA_STREAM_ADJUST_LATENCY |
									   PA_STREAM_AUTO_TIMING_UPDATE));
	}

	if (ret < 0)
	{
		contextError(mContext);
	}

	waitStreamReady();
}

void PulsePcm::close()
{
	lock_guard<PulseMutex> lock(mMutex);

	if (mStream)
	{
		DLOG(mLog, DEBUG) << "Close pcm device: " << mName;

		// drain
		if (mType == StreamType::PLAYBACK)
		{
			drain();
		}

		pa_stream_disconnect(mStream);

		pa_stream_set_state_callback(mStream, nullptr, nullptr);

		pa_stream_unref(mStream);

		mStream = nullptr;
	}
}

void PulsePcm::read(uint8_t* buffer, size_t size)
{
	DLOG(mLog, DEBUG) << "Read from pcm device: " << mName
					  << ", size: " << size;

	if (mType != StreamType::CAPTURE)
	{
		throw SoundException(pa_strerror(PA_ERR_BADSTATE), -PA_ERR_BADSTATE);
	}

	if (!buffer || !size)
	{
		throw SoundException(pa_strerror(PA_ERR_INVALID), -PA_ERR_INVALID);
	}

	lock_guard<PulseMutex> lock(mMutex);

	checkStatus();

	while (size > 0)
	{
		while (!mReadData)
		{
			if (pa_stream_peek(mStream, &mReadData, &mReadLength) < 0)
			{
				contextError(mContext);
			}

			if (mReadLength <= 0)
			{
				pa_threaded_mainloop_wait(mMainloop);

				checkStatus();
			}
			else if (!mReadData)
			{
				if (pa_stream_drop(mStream) < 0)
				{
					contextError(mContext);
				}
			} else
			{
				mReadIndex = 0;
			}
		}

		size_t readableSize = mReadLength < size ? mReadLength : size;

		memcpy(buffer, static_cast<const uint8_t*>(mReadData) + mReadIndex,
			   readableSize);

		buffer = buffer + readableSize;
		size -= readableSize;

		mReadIndex += readableSize;
		mReadLength -= readableSize;

		if (mReadLength == 0)
		{
			mReadData = nullptr;
			mReadLength = 0;
			mReadIndex = 0;

			if (pa_stream_drop(mStream) < 0)
			{
				contextError(mContext);
			}
		}
	}
}

void PulsePcm::write(uint8_t* buffer, size_t size)
{
	DLOG(mLog, DEBUG) << "Write to pcm device: " << mName
					  << ", size: " << size;

	if (mType != StreamType::PLAYBACK)
	{
		throw SoundException(pa_strerror(PA_ERR_BADSTATE), -PA_ERR_BADSTATE);
	}

	if (!buffer || !size)
	{
		throw SoundException(pa_strerror(PA_ERR_INVALID), -PA_ERR_INVALID);
	}

	lock_guard<PulseMutex> lock(mMutex);

	checkStatus();

	while (size > 0)
	{
		size_t writableSize;

		while ((writableSize = pa_stream_writable_size(mStream)) == 0)
		{
			pa_threaded_mainloop_wait(mMainloop);

			checkStatus();
		}

		if (writableSize == static_cast<size_t>(-1))
		{
			contextError(mContext);
		}

		if (writableSize > size)
		{
			writableSize = size;
		}

		if (pa_stream_write(mStream, buffer, writableSize, nullptr,
							0LL, PA_SEEK_RELATIVE) < 0)
		{
			contextError(mContext);
		}

		buffer = buffer + writableSize;

		size -= writableSize;
	}
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void PulsePcm::sStreamStateChanged(pa_stream *stream, void *data)
{
	static_cast<PulsePcm*>(data)->streamStateChanged();
}

void PulsePcm::sStreamRequest(pa_stream *stream, size_t nbytes, void *data)
{
	static_cast<PulsePcm*>(data)->streamRequest(nbytes);
}

void PulsePcm::sLatencyUpdate(pa_stream *stream, void *data)
{
	static_cast<PulsePcm*>(data)->latencyUpdate();
}

void PulsePcm::sSuccessCbk(pa_stream* stream, int success, void *data)
{
	static_cast<PulsePcm*>(data)->successCbk(success);
}

void PulsePcm::streamStateChanged()
{
	switch (pa_stream_get_state(mStream))
	{
		case PA_STREAM_READY:
		case PA_STREAM_FAILED:
		case PA_STREAM_TERMINATED:

			pa_threaded_mainloop_signal(mMainloop, 0);

			break;

		default:
			break;
	}
}

void PulsePcm::streamRequest(size_t nbytes)
{
    pa_threaded_mainloop_signal(mMainloop, 0);
}

void PulsePcm::latencyUpdate()
{
    pa_threaded_mainloop_signal(mMainloop, 0);
}

void PulsePcm::successCbk(int success)
{
	mSuccess = success;

	pa_threaded_mainloop_signal(mMainloop, 0);
}

void PulsePcm::waitStreamReady()
{
	for (;;)
	{
		auto state = pa_stream_get_state(mStream);

		if (state == PA_STREAM_READY)
		{
			break;
		}

		if (!PA_STREAM_IS_GOOD(state))
		{
			contextError(mContext);
		}

		pa_threaded_mainloop_wait(mMainloop);
	}
}

void PulsePcm::drain()
{
	auto op = pa_stream_drain(mStream, sSuccessCbk, this);

	if (op)
	{
		waitOperationFinished(op);

		pa_operation_unref(op);
	}
}

int PulsePcm::waitOperationFinished(pa_operation* op)
{
	int states = PA_OK;

	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING &&
		   (states  = getStatus()) == PA_OK)
	{
		pa_threaded_mainloop_wait(mMainloop);
	}

	if (states != PA_OK)
	{
		return states;
	}

	return mSuccess;
}

int PulsePcm::getStatus()
{
	if (!mContext || !PA_CONTEXT_IS_GOOD(pa_context_get_state(mContext)) ||
		!mStream || !PA_STREAM_IS_GOOD(pa_stream_get_state(mStream)))
	{
		int error = PA_ERR_BADSTATE;

		if ((mContext && pa_context_get_state(mContext) == PA_CONTEXT_FAILED) ||
			(mStream && pa_stream_get_state(mStream) == PA_STREAM_FAILED))
		{
			error = pa_context_errno(mContext);
		}

		return error;
	}

	return PA_OK;
}

void PulsePcm::checkStatus()
{
	auto error = getStatus();

	if (error != PA_OK)
	{
		throw SoundException(pa_strerror(error), -error);
	}
}

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

	throw SoundException("Can't convert format", -PA_ERR_INVALID);
}

}

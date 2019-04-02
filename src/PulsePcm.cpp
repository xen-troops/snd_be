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
using SoundItf::StreamType;

namespace Pulse {

void contextError(const string& message, pa_context* context)
{
	throw Exception(message, pa_context_errno(context));
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
	catch(const std::exception& e)
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
			contextError("Can't wait context ready", mContext);
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
		throw Exception("Can't create Pulse mainloop", PA_ERR_UNKNOWN);
	}

	mMutex = PulseMutex(mMainloop);

	auto api = pa_threaded_mainloop_get_api(mMainloop);

	if (!api)
	{
		throw Exception("Can't get Pulse API", PA_ERR_UNKNOWN);
	}

	mContext = pa_context_new(api, name.c_str());

	if (!mContext)
	{
		throw Exception("Can't create Pulse context", PA_ERR_UNKNOWN);
	}

	pa_context_set_state_callback(mContext, sContextStateChanged, this);

	if (pa_context_connect(mContext, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
	{
		contextError("Can't connect context", mContext);
	}

	lock_guard<PulseMutex> lock(mMutex);

	if (pa_threaded_mainloop_start(mMainloop) < 0)
	{
		throw Exception("Can't start Pulse mainloop", PA_ERR_UNKNOWN);
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
	mTimeEvent(nullptr),
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

	LOG(mLog, DEBUG) << "Open pcm device: " << mName;

	LOG(mLog, DEBUG) << "Format: "
	  << pa_sample_format_to_string(convertPcmFormat(params.format))
	  << ", rate: " << params.rate
	  << ", channels: " << static_cast<int>(params.numChannels)
	  << ", period: " << params.periodSize
	  << ", buffer: " << params.bufferSize;

	mReadData = nullptr;
	mReadIndex = 0;
	mReadLength = 0;

	mParams = params;

	createStream();

	const char* deviceName = nullptr;

	if (!mDeviceName.empty())
	{
		deviceName = mDeviceName.c_str();
	}

	if (mType == StreamType::PLAYBACK)
	{
		connectPlaybackStream(deviceName);
	}
	else
	{
		connectCaptureStream(deviceName);
	}

	waitStreamReady();
}

void PulsePcm::close()
{
	lock_guard<PulseMutex> lock(mMutex);

	stopTimer();

	if (mStream)
	{
		LOG(mLog, DEBUG) << "Close pcm device: " << mName;

		// drain
		if (mType == StreamType::PLAYBACK)
		{
			flush();
		}

		pa_stream_disconnect(mStream);

		pa_threaded_mainloop_wait(mMainloop);

		pa_stream_set_state_callback(mStream, nullptr, nullptr);
		pa_stream_set_write_callback(mStream, nullptr, nullptr);
		pa_stream_set_latency_update_callback(mStream, nullptr, nullptr);
		pa_stream_set_read_callback(mStream, nullptr, nullptr);

		pa_stream_unref(mStream);

		mStream = nullptr;
	}
}

void PulsePcm::read(uint8_t* buffer, size_t size)
{
	lock_guard<PulseMutex> lock(mMutex);

	DLOG(mLog, DEBUG) << "Read from pcm device: " << mName
					  << ", size: " << size;

	if (mType != StreamType::CAPTURE)
	{
		throw Exception("Wrong stream type", PA_ERR_BADSTATE);
	}

	if (!buffer || !size)
	{
		throw Exception("Can't read stream", PA_ERR_INVALID);
	}

	checkStatus();

	while (size > 0)
	{
		while (!mReadData)
		{
			if (pa_stream_peek(mStream, &mReadData, &mReadLength) < 0)
			{
				contextError("Can't peek stream", mContext);
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
					contextError("Can't drop stream", mContext);
				}
			}
			else
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
				contextError("Can't drop stream", mContext);
			}
		}
	}
}

void PulsePcm::write(uint8_t* buffer, size_t size)
{
	lock_guard<PulseMutex> lock(mMutex);

	DLOG(mLog, DEBUG) << "Write to pcm device: " << mName
					  << ", size: " << size;

	if (mType != StreamType::PLAYBACK)
	{
		throw Exception("Wrong stream type", PA_ERR_BADSTATE);
	}

	if (!buffer || !size)
	{
		throw Exception("Can't write stream", PA_ERR_INVALID);
	}

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
			contextError("Can't write stream", mContext);
		}

		if (writableSize > size)
		{
			writableSize = size;
		}

		if (pa_stream_write(mStream, buffer, writableSize, nullptr,
							0LL, PA_SEEK_RELATIVE) < 0)
		{
			contextError("Can't write stream", mContext);
		}

		buffer = buffer + writableSize;

		size -= writableSize;
	}
}

void PulsePcm::start()
{
	lock_guard<PulseMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Start";

	auto op = pa_stream_cork(mStream, 0, sSuccessCbk, this);

	if (!op)
	{
		contextError("Can't start stream", mContext);
	}

	if (!waitOperationFinished(op))
	{
		contextError("Can't start stream", mContext);
	}

	pa_operation_unref(op);

	startTimer();
}

void PulsePcm::stop()
{
	lock_guard<PulseMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Stop";

	auto op = pa_stream_cork(mStream, 1, sSuccessCbk, this);

	if (!op)
	{
		contextError("Can't stop stream", mContext);
	}

	if (!waitOperationFinished(op))
	{
		contextError("Can't stop stream", mContext);
	}

	pa_operation_unref(op);

	flush();

	stopTimer();
}

void PulsePcm::pause()
{
	lock_guard<PulseMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Pause";

	auto op = pa_stream_cork(mStream, 1, sSuccessCbk, this);

	if (!op)
	{
		contextError("Can't pause stream", mContext);
	}

	if (!waitOperationFinished(op))
	{
		contextError("Can't pause stream", mContext);
	}

	pa_operation_unref(op);
}

void PulsePcm::resume()
{
	lock_guard<PulseMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Resume";

	auto op = pa_stream_cork(mStream, 0, sSuccessCbk, this);

	if (!op)
	{
		contextError("Can't resume stream", mContext);
	}

	if (!waitOperationFinished(op))
	{
		contextError("Can't resume stream", mContext);
	}

	pa_operation_unref(op);
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

void PulsePcm::sTimeEventCbk(pa_mainloop_api *api, pa_time_event *timeEvent,
							 const struct timeval *tv, void *data)
{
	static_cast<PulsePcm*>(data)->timeEventCbk(api, timeEvent, tv);
}

void PulsePcm::sUpdateTimingCbk(pa_stream *stream, int success, void *data)
{
	static_cast<PulsePcm*>(data)->updateTimingCbk(success);
}

void PulsePcm::streamStateChanged()
{
	auto state = pa_stream_get_state(mStream);

	LOG(mLog, DEBUG) << "Stream state changed: " << state;

	switch (state)
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

void PulsePcm::timeEventCbk(pa_mainloop_api *api, pa_time_event *timeEvent,
							const struct timeval *tv)
{
	if (mStream && pa_stream_get_state(mStream) == PA_STREAM_READY)
	{
		auto op = pa_stream_update_timing_info(mStream, sUpdateTimingCbk, this);

		if (!op)
		{
			LOG(mLog, ERROR) << "Can't update timing info";
		}
		else
		{
			pa_operation_unref(op);
		}
	}

	timeval now;

	gettimeofday(&now, nullptr);
	pa_timeval_add(&now, pa_bytes_to_usec(mParams.periodSize, &mSampleSpec));

	api->time_restart(timeEvent, &now);
}

void PulsePcm::updateTimingCbk(int success)
{
	pa_usec_t time;

	pa_stream_get_time(mStream, &time);

	auto bytes = pa_usec_to_bytes(time, &mSampleSpec);

	if (mProgressCbk && !pa_stream_is_corked(mStream))
	{
		DLOG(mLog, DEBUG) << "Update timing, usec: " << time / 1000
						  << ", bytes: " << bytes;

		mProgressCbk(bytes);
	}
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
			contextError("Can't wait stream ready", mContext);
		}

		pa_threaded_mainloop_wait(mMainloop);
	}
}

void PulsePcm::flush()
{
	auto op = pa_stream_flush(mStream, sSuccessCbk, this);

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
		   (states = getStatus()) == PA_OK)
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
		throw Exception("Stream error", error);
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

	throw Exception("Can't convert format", PA_ERR_INVALID);
}

void PulsePcm::startTimer()
{
	timeval now;

	gettimeofday(&now, nullptr);
	pa_timeval_add(&now, pa_bytes_to_usec(mParams.periodSize, &mSampleSpec));

	auto api = pa_threaded_mainloop_get_api(mMainloop);
	mTimeEvent = api->time_new(api, &now, sTimeEventCbk, this);

	if (!mTimeEvent)
	{
		throw Exception("Can't create time event " + mName, PA_ERR_UNKNOWN);
	}
}

void PulsePcm::stopTimer()
{
	if (mTimeEvent)
	{
		auto api = pa_threaded_mainloop_get_api(mMainloop);
		api->time_free(mTimeEvent);

		mTimeEvent = nullptr;
	}
}

void PulsePcm::createStream()
{
	if (mStream)
	{
		throw Exception("PCM device " + mName + " already opened", PA_ERR_EXIST);
	}

	mSampleSpec.format = convertPcmFormat(mParams.format);
	mSampleSpec.rate = mParams.rate;
	mSampleSpec.channels = mParams.numChannels;

	PulseProplist propList(mPropName.c_str(), mPropValue.c_str());

	mStream = pa_stream_new_with_proplist(mContext, mName.c_str(), &mSampleSpec,
										  nullptr, &propList);

	if (!mStream)
	{
		throw Exception("Can't open PCM device " + mName, PA_ERR_UNKNOWN);
	}

	pa_stream_set_state_callback(mStream, sStreamStateChanged, this);
}

void PulsePcm::connectPlaybackStream(const char* deviceName)
{
	pa_buffer_attr bufferAttr;

	bufferAttr.maxlength = -1; //mParams.bufferSize ? mParams.bufferSize : -1;
	bufferAttr.tlength = -1;
	bufferAttr.prebuf = 0;
	bufferAttr.minreq = -1;
	bufferAttr.fragsize = -1;

	pa_stream_set_write_callback(mStream, sStreamRequest, this);
	pa_stream_set_latency_update_callback(mStream, sLatencyUpdate, this);

	if (pa_stream_connect_playback(mStream, deviceName, &bufferAttr,
								   static_cast<pa_stream_flags_t>(
								   PA_STREAM_START_CORKED |
								   PA_STREAM_INTERPOLATE_TIMING |
								   PA_STREAM_ADJUST_LATENCY |
								   PA_STREAM_AUTO_TIMING_UPDATE),
								   nullptr, nullptr) < 0)
	{
		contextError("Can't connect to device: " + mDeviceName, mContext);
	}
}

void PulsePcm::connectCaptureStream(const char* deviceName)
{
	pa_stream_set_read_callback(mStream, sStreamRequest, this);

	if (pa_stream_connect_record(mStream, deviceName, nullptr,
								 static_cast<pa_stream_flags_t>(
								 PA_STREAM_INTERPOLATE_TIMING |
								 PA_STREAM_ADJUST_LATENCY |
								 PA_STREAM_AUTO_TIMING_UPDATE)) < 0)
	{
		contextError("Can't connect to device: " + mDeviceName, mContext);
	}
}

void PulsePcm::queryHwRanges(SoundItf::PcmParamRanges& req, SoundItf::PcmParamRanges& resp)
{
	resp = req;
}

}

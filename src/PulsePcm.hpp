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

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include <xen/be/Log.hpp>

#include "SoundItf.hpp"

namespace Pulse {

class PulsePcm;

/***************************************************************************//**
 * @defgroup pulse
 * PulseAudio related classes.
 ******************************************************************************/

/***************************************************************************//**
 * Wrapper for main loop lock
 * @ingroup pulse
 ******************************************************************************/
class PulseMutex
{
public:

	PulseMutex(pa_threaded_mainloop* mainloop) : mMainloop(mainloop) {}
	void lock() { pa_threaded_mainloop_lock(mMainloop); }
	void unlock() { pa_threaded_mainloop_unlock(mMainloop); }

private:

	pa_threaded_mainloop* mMainloop;
};

/***************************************************************************//**
 * Wrapper for Pulse prop list
 * @ingroup pulse
 ******************************************************************************/
class PulseProplist
{
public:

	PulseProplist() { mProplist = pa_proplist_new(); }

	PulseProplist(const std::string& name, const std::string& value) : PulseProplist()
    {
        set(name, value);
    }

	~PulseProplist() { pa_proplist_free(mProplist); }

    int set(const std::string& name, const std::string& value)
    {
        return pa_proplist_sets(mProplist, name.c_str(), value.c_str());
    }

    pa_proplist* operator&() { return mProplist; }

private:
    pa_proplist* mProplist;
};

/***************************************************************************//**
 * PulseAudio main loop
 * @ingroup pulse
 ******************************************************************************/
class PulseMainloop
{
public:

	PulseMainloop(const std::string& name);
	~PulseMainloop();

	PulsePcm* createStream(SoundItf::StreamType type, const std::string& name,
						   const std::string& propName = "",
						   const std::string& propValue = "");

private:

	pa_threaded_mainloop* mMainloop;
	pa_context* mContext;
	PulseMutex mMutex;

	XenBackend::Log mLog;

	static void sContextStateChanged(pa_context *context, void *data);
	void contextStateChanged();

	void waitContextReady();
	void init(const std::string& name);
	void release();
};

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
	PulsePcm(pa_threaded_mainloop* mainloop, pa_context* context,
			 SoundItf::StreamType type,
			 const std::string& name,
			 const std::string& propName,
			 const std::string& propValue);

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
	void read(uint8_t* buffer, size_t size) override;

	/**
	 * Writes data to the pcm device.
	 * @param buffer buffer with data
	 * @param size   number of bytes to write
	 */
	void write(uint8_t* buffer, size_t size) override;

private:

	struct PcmFormat
	{
		uint8_t sndif;
		pa_sample_format_t pulse;
	};

	static PcmFormat sPcmFormat[];

	pa_threaded_mainloop* mMainloop;
	pa_context*  mContext;
	pa_stream* mStream;
	int mSuccess;
	PulseMutex mMutex;
	SoundItf::StreamType mType;
	std::string mName;
	std::string mPropName;
	std::string mPropValue;
	const void* mReadData;
	size_t mReadIndex;
	size_t mReadLength;

	XenBackend::Log mLog;

	static void sStreamStateChanged(pa_stream *stream, void *data);
	static void sStreamRequest(pa_stream *stream, size_t nbytes, void *data);
	static void sLatencyUpdate(pa_stream *stream, void *data);
	static void sSuccessCbk(pa_stream* stream, int success, void *data);

	void streamStateChanged();
	void streamRequest(size_t nbytes);
	void latencyUpdate();
	void successCbk(int success);

	void waitStreamReady();
	void drain();
	int waitOperationFinished(pa_operation* op);
	int getStatus();
	void checkStatus();

	pa_sample_format_t convertPcmFormat(uint8_t format);
};

}

#endif /* SRC_PULSEPCM_HPP_ */

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

#include <xen/io/sndif.h>

using std::bind;
using std::chrono::milliseconds;
using std::string;
using std::to_string;

using SoundItf::PcmParams;
using SoundItf::StreamType;

namespace Alsa {

/*******************************************************************************
 * AlsaPcm
 ******************************************************************************/

AlsaPcm::AlsaPcm(StreamType type, const std::string& deviceName) :
	mHandle(nullptr),
	mDeviceName(deviceName),
	mType(type),
	mTimer(bind(&AlsaPcm::getTimeStamp, this), true),
	mLog("AlsaPcm"),
	mHwQueryHandle(nullptr),
	mHwQueryParams(nullptr)
{
	if (mDeviceName.empty())
	{
		mDeviceName = "default";
	}

	LOG(mLog, DEBUG) << "Create pcm device: " << mDeviceName;
}

AlsaPcm::~AlsaPcm()
{
	LOG(mLog, DEBUG) << "Delete pcm device: " << mDeviceName;

	close();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

void AlsaPcm::queryHwRanges(SoundItf::PcmParamRanges& req, SoundItf::PcmParamRanges& resp)
{
	snd_pcm_hw_params_t* hwParams;

	queryOpen();

	DLOG(mLog, DEBUG) << "Query pcm device " << mDeviceName << " for HW parameters";

	snd_pcm_hw_params_alloca(&hwParams);
	snd_pcm_hw_params_copy(hwParams, mHwQueryParams);

	queryHwParamFormats(hwParams, req, resp);
	queryHwParamRate(hwParams, req, resp);
	queryHwParamChannels(hwParams, req, resp);
	queryHwParamBuffer(hwParams, req, resp);
	queryHwParamPeriod(hwParams, req, resp);
}

void AlsaPcm::open(const PcmParams& params)
{
	try
	{
		DLOG(mLog, DEBUG) << "Open pcm device: " << mDeviceName;

		snd_pcm_stream_t streamType = mType == StreamType::PLAYBACK ?
				SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE;

		int ret = 0;

		queryClose();

		if ((ret = snd_pcm_open(&mHandle, mDeviceName.c_str(),
							    streamType, 0)) < 0)
		{
			throw Exception("Can't open audio device " + mDeviceName, -ret);
		}

		setHwParams(params);
		setSwParams();

		if ((ret = snd_pcm_prepare(mHandle)) < 0)
		{
			throw Exception("Can't prepare audio interface for use", -ret);
		}

		mFrameWritten = 0;
		mFrameUnderrun = 0;
	}
	catch(const std::exception& e)
	{
		close();

		throw;
	}
}

void AlsaPcm::close()
{
	queryClose();

	if (mHandle)
	{
		DLOG(mLog, DEBUG) << "Close pcm device: " << mDeviceName;

		snd_pcm_drain(mHandle);

		mTimer.stop();

		snd_pcm_close(mHandle);
	}

	mHandle = nullptr;
}

void AlsaPcm::read(uint8_t* buffer, size_t size)
{
	DLOG(mLog, DEBUG) << "Read from pcm device: " << mDeviceName
					  << ", size: " << size;

	if (!mHandle)
	{
		throw Exception("Alsa device is not opened: " + mDeviceName, EFAULT);
	}

	auto numFrames = snd_pcm_bytes_to_frames(mHandle, size);

	while(numFrames > 0)
	{
		if (auto status = snd_pcm_readi(mHandle, buffer, numFrames))
		{
			if (status == -EPIPE)
			{
				LOG(mLog, WARNING) << "Device: " << mDeviceName
								   << ", message: " << snd_strerror(status);

				snd_pcm_prepare(mHandle);
			}
			else if (status < 0)
			{
				throw Exception("Read from audio interface failed: " +
								 mDeviceName, -status);
			}
			else
			{
				numFrames -= status;
				buffer = &buffer[snd_pcm_frames_to_bytes(mHandle, status)];
			}
		}
	}
}

void AlsaPcm::write(uint8_t* buffer, size_t size)
{
	if (!mHandle)
	{
		throw Exception("Alsa device is not opened: " + mDeviceName, EFAULT);
	}

	auto numFrames = snd_pcm_bytes_to_frames(mHandle, size);

	bool restartAfterError = false;

	while(numFrames > 0)
	{
		if (auto status = snd_pcm_writei(mHandle, buffer, numFrames))
		{
			DLOG(mLog, DEBUG) << "Write to pcm device: " << mDeviceName
							  << ", size: " << status;


			if (status == -EPIPE)
			{
				LOG(mLog, WARNING) << "Device: " << mDeviceName
								   << ", message: " << snd_strerror(status);

				if ((status = snd_pcm_recover(mHandle, status, 0)) < 0)
				{
					throw Exception("Can't recover underrun: " + mDeviceName,
									-status);
				}

				mFrameUnderrun = mFrameWritten;

				restartAfterError = true;
			}
			else if (status < 0)
			{
				throw Exception("Write to audio interface failed: " +
								mDeviceName, -status);
			}
			else
			{
				numFrames -= status;
				buffer = &buffer[snd_pcm_frames_to_bytes(mHandle, status)];
				mFrameWritten += status;

				if (snd_pcm_state(mHandle) != SND_PCM_STATE_RUNNING &&
					restartAfterError)
				{
					restartAfterError = false;

					if ((status = snd_pcm_start(mHandle)) < 0)
					{
						throw Exception("Can't recover underrun: " +
										mDeviceName, -status);
					}
				}
			}
		}
	}
}

void AlsaPcm::start()
{
	LOG(mLog, DEBUG) << "Start";

	if (!mHandle)
	{
		throw Exception("Alsa device is not opened: " +
						 mDeviceName , EFAULT);
	}

	int ret = 0;

	if ((ret = snd_pcm_start(mHandle)) < 0)
	{
		throw Exception("Can't start device " + mDeviceName, -ret);
	}

	auto time =
			(snd_pcm_bytes_to_frames(mHandle, mParams.periodSize) * 1000) /
			mParams.rate;

	mTimer.start(milliseconds(time));
}

void AlsaPcm::stop()
{
	LOG(mLog, DEBUG) << "Stop";

	if (!mHandle)
	{
		throw Exception("Alsa device is not opened: " +
						 mDeviceName, EFAULT);
	}

	int ret = 0;

	if ((ret = snd_pcm_drop(mHandle)) < 0)
	{
		throw Exception("Can't stop device " + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_prepare(mHandle)) < 0)
	{
		throw Exception("Can't prepare audio interface for use", -ret);
	}

	mTimer.stop();
}

void AlsaPcm::pause()
{
	LOG(mLog, DEBUG) << "Pause";

	if (!mHandle)
	{
		throw Exception("Alsa device is not opened: " +
						 mDeviceName, EFAULT);
	}

	int ret = 0;

	if ((ret = snd_pcm_pause(mHandle, 1)) < 0)
	{
		throw Exception("Can't pause device " + mDeviceName, -ret);
	}
}

void AlsaPcm::resume()
{
	LOG(mLog, DEBUG) << "Resume";

	if (!mHandle)
	{
		throw Exception("Alsa device is not opened: " +
						 mDeviceName, EFAULT);
	}

	int ret = 0;

	if ((ret = snd_pcm_pause(mHandle, 0)) < 0)
	{
		throw Exception("Can't resume device " + mDeviceName, -ret);
	}
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void AlsaPcm::setHwParams(const PcmParams& params)
{
	LOG(mLog, DEBUG) << "Format: "
	  << snd_pcm_format_name(convertPcmFormat(params.format))
	  << ", rate: " << params.rate
	  << ", channels: " << static_cast<int>(params.numChannels)
	  << ", period: " << params.periodSize
	  << ", buffer: " << params.bufferSize;

	snd_pcm_hw_params_t* hwParams = nullptr;

	int ret = 0;

	mParams = params;

	snd_pcm_hw_params_alloca(&hwParams);

	if ((ret = snd_pcm_hw_params_any(mHandle, hwParams)) < 0)
	{
		throw Exception("Can't fill hw params " + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_hw_params_set_access(
			mHandle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
	{
		throw Exception("Can't set access " + mDeviceName, -ret);
	}

	snd_pcm_format_t format = convertPcmFormat(params.format);

	if ((ret = snd_pcm_hw_params_set_format(
			mHandle, hwParams, format)) < 0)
	{
		throw Exception("Can't set format " + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_hw_params_set_rate(
			mHandle, hwParams, params.rate, 0)) < 0)
	{
		throw Exception("Can't set rate " + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_hw_params_set_channels(
			mHandle, hwParams, params.numChannels)) < 0)
	{
		throw Exception("Can't set num channels " + mDeviceName, -ret);
	}

	snd_pcm_uframes_t bufferFrames = cDefaultBufferFrames;

	if (params.bufferSize)
	{
		bufferFrames = params.bufferSize /
					   (params.numChannels  * snd_pcm_format_size(format, 1));
	}

	if ((ret = snd_pcm_hw_params_set_buffer_size_near(
			mHandle, hwParams, &bufferFrames)) < 0)
	{
		throw Exception("Can't set buffer size " + mDeviceName, -ret);
	}

	mParams.bufferSize = bufferFrames *
						 (params.numChannels  * snd_pcm_format_size(format, 1));

	if (params.bufferSize && params.bufferSize != mParams.bufferSize)
	{
		LOG(mLog, WARNING) << "Can't set requested buffer size. "
						   << "Nearest value will be used: "
						   << mParams.bufferSize;
	}

	snd_pcm_uframes_t periodFrames = cDefaultPeriodFrames;

	if (params.periodSize)
	{
		periodFrames = params.periodSize /
					   (params.numChannels  * snd_pcm_format_size(format, 1));
	}

	if ((ret = snd_pcm_hw_params_set_period_size_near(
			mHandle, hwParams, &periodFrames, 0)) < 0)
	{
		throw Exception("Can't set period size " + mDeviceName, -ret);
	}

	mParams.periodSize = periodFrames *
						 (params.numChannels  * snd_pcm_format_size(format, 1));

	if (params.periodSize && params.periodSize != mParams.periodSize)
	{
		LOG(mLog, WARNING) << "Can't set requested period size. "
						   << "Nearest value will be used: "
						   << mParams.periodSize;
	}

	if ((ret = snd_pcm_hw_params(mHandle, hwParams)) < 0)
	{
		throw Exception("Can't set hw params " + mDeviceName, -ret);
	}

	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 0))
		LOG(mLog, DEBUG) << "Playback supports audio compat timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 1))
		LOG(mLog, DEBUG) << "Playback supports audio default timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 2))
		LOG(mLog, DEBUG) << "Playback supports audio link timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 3))
		LOG(mLog, DEBUG) << "Playback supports audio link absolute timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 4))
		LOG(mLog, DEBUG) << "Playback supports audio link estimated timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 5))
		LOG(mLog, DEBUG) << "Playback supports audio link synchronized timestamps";
}

void AlsaPcm::setSwParams()
{
	snd_pcm_sw_params_t* swParams = nullptr;

	int ret = 0;

	snd_pcm_sw_params_alloca(&swParams);

	if ((ret = snd_pcm_sw_params_current(mHandle, swParams)) < 0)
	{
		throw Exception("Can't get swParams " + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_sw_params_set_tstamp_mode(
			mHandle, swParams, SND_PCM_TSTAMP_ENABLE)) < 0)
	{
		throw Exception("Can't set ts mode " + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_sw_params_set_tstamp_type(
			mHandle, swParams, SND_PCM_TSTAMP_TYPE_MONOTONIC_RAW)) < 0)
	{
		throw Exception("Can't set ts type " + mDeviceName, -ret);
	}

	snd_pcm_uframes_t threshold =
			snd_pcm_bytes_to_frames(mHandle, mParams.bufferSize) * 2;

	if ((ret = snd_pcm_sw_params_set_start_threshold(
			mHandle, swParams, threshold)) < 0)
	{
		throw Exception("Can't set start threshold " + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_sw_params(mHandle, swParams)) < 0)
	{
		throw Exception("Can't set swParams " + mDeviceName, -ret);
	}
}

void AlsaPcm::getTimeStamp()
{
	snd_pcm_status_t* status;

	int ret = 0;

	snd_pcm_status_alloca(&status);

	if ((ret = snd_pcm_status(mHandle, status)) < 0)
	{
		LOG(mLog, ERROR) << "Can't get status. Err: " << ret;
	}

	auto state = snd_pcm_status_get_state(status);

	snd_htimestamp_t audioTimeStamp;

	snd_pcm_status_get_audio_htstamp(status, &audioTimeStamp);

	uint64_t frame = ((audioTimeStamp.tv_sec * 1000000000 +
					 audioTimeStamp.tv_nsec) * mParams.rate) / 1000000000;

	frame += mFrameUnderrun;

	uint64_t bytes;

	if (state == SND_PCM_STATE_XRUN)
	{
		bytes = snd_pcm_frames_to_bytes(mHandle, mFrameWritten);
	}
	else
	{
		bytes = snd_pcm_frames_to_bytes(mHandle, frame);
	}

	LOG(mLog, DEBUG) << "Frame: " << frame
					 << ", bytes: " << bytes
					 << ", state: " << state;

	if (mProgressCbk)
	{
		mProgressCbk(bytes);
	}
}

AlsaPcm::PcmFormat AlsaPcm::sPcmFormat[] =
{
	{XENSND_PCM_FORMAT_U8,                 SND_PCM_FORMAT_U8 },
	{XENSND_PCM_FORMAT_S8,                 SND_PCM_FORMAT_S8 },
	{XENSND_PCM_FORMAT_U16_LE,             SND_PCM_FORMAT_U16_LE },
	{XENSND_PCM_FORMAT_U16_BE,             SND_PCM_FORMAT_U16_BE },
	{XENSND_PCM_FORMAT_S16_LE,             SND_PCM_FORMAT_S16_LE },
	{XENSND_PCM_FORMAT_S16_BE,             SND_PCM_FORMAT_S16_BE },
	{XENSND_PCM_FORMAT_U24_LE,             SND_PCM_FORMAT_U24_LE },
	{XENSND_PCM_FORMAT_U24_BE,             SND_PCM_FORMAT_U24_BE },
	{XENSND_PCM_FORMAT_S24_LE,             SND_PCM_FORMAT_S24_LE },
	{XENSND_PCM_FORMAT_S24_BE,             SND_PCM_FORMAT_S24_BE },
	{XENSND_PCM_FORMAT_U32_LE,             SND_PCM_FORMAT_U32_LE },
	{XENSND_PCM_FORMAT_U32_BE,             SND_PCM_FORMAT_U32_BE },
	{XENSND_PCM_FORMAT_S32_LE,             SND_PCM_FORMAT_S32_LE },
	{XENSND_PCM_FORMAT_S32_BE,             SND_PCM_FORMAT_S32_BE },
	{XENSND_PCM_FORMAT_A_LAW,              SND_PCM_FORMAT_A_LAW },
	{XENSND_PCM_FORMAT_MU_LAW,             SND_PCM_FORMAT_MU_LAW },
	{XENSND_PCM_FORMAT_F32_LE,             SND_PCM_FORMAT_FLOAT_LE },
	{XENSND_PCM_FORMAT_F32_BE,             SND_PCM_FORMAT_FLOAT_BE },
	{XENSND_PCM_FORMAT_F64_LE,             SND_PCM_FORMAT_FLOAT64_LE },
	{XENSND_PCM_FORMAT_F64_BE,             SND_PCM_FORMAT_FLOAT64_BE },
	{XENSND_PCM_FORMAT_IEC958_SUBFRAME_LE, SND_PCM_FORMAT_IEC958_SUBFRAME_LE },
	{XENSND_PCM_FORMAT_IEC958_SUBFRAME_BE, SND_PCM_FORMAT_IEC958_SUBFRAME_BE },
	{XENSND_PCM_FORMAT_IMA_ADPCM,          SND_PCM_FORMAT_IMA_ADPCM },
	{XENSND_PCM_FORMAT_MPEG,               SND_PCM_FORMAT_MPEG },
	{XENSND_PCM_FORMAT_GSM,                SND_PCM_FORMAT_GSM },
};

snd_pcm_format_t AlsaPcm::convertPcmFormat(uint8_t format)
{
	for (auto value : sPcmFormat)
	{
		if (value.sndif == format)
		{
			return value.alsa;
		}
	}

	throw Exception("Can't convert format", EINVAL);
}

void AlsaPcm::queryOpen()
{
	try
	{
		snd_pcm_stream_t streamType = mType == StreamType::PLAYBACK ?
				SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE;

		int ret = 0;

		if (!mHwQueryHandle)
		{
			DLOG(mLog, DEBUG) << "Opening pcm device for queries: " << mDeviceName;

			if ((ret = snd_pcm_open(&mHwQueryHandle, mDeviceName.c_str(),
								    streamType, 0)) < 0)
			{
				throw Exception("Can't open audio device " + mDeviceName, -ret);
			}

			/*
			 * remember this, so next time we need to query we just use
			 * this copy and save one ioctl call
			 */
			snd_pcm_hw_params_malloc(&mHwQueryParams);

			if ((ret = snd_pcm_hw_params_any(mHwQueryHandle, mHwQueryParams)) < 0)
			{
				throw Exception("Can't fill hw params " + mDeviceName, -ret);
			}
		}
	}
	catch(const std::exception& e)
	{
		close();

		throw;
	}
}

void AlsaPcm::queryClose()
{
	if (mHwQueryHandle)
	{
		DLOG(mLog, DEBUG) << "Close pcm query device: " << mDeviceName;

		snd_pcm_close(mHwQueryHandle);
	}

	if (mHwQueryParams)
	{
		snd_pcm_hw_params_free(mHwQueryParams);
	}

	mHwQueryHandle = nullptr;
	mHwQueryParams = nullptr;
}

void AlsaPcm::queryHwParamRate(snd_pcm_hw_params_t* hwParams,
							   SoundItf::PcmParamRanges& req,
							   SoundItf::PcmParamRanges& resp)
{
	int ret;

	if ((ret = snd_pcm_hw_params_set_rate_minmax(mHwQueryHandle, hwParams,
			&req.rates.min, 0, &req.rates.max, 0)) < 0)
	{
		/*
		 * This is not really a fatal error, the frontend just tries to
		 * reduce configuration space.
		 * But, anyway, throw now, so we can return error code to
		 * the frontend.
		 */
		throw;
	}

	if ((ret = snd_pcm_hw_params_get_rate_min(hwParams, &resp.rates.min, 0)) < 0)
	{
		throw Exception("Can't get rate min " + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_hw_params_get_rate_max(mHwQueryParams, &resp.rates.max, 0)) < 0)
	{
		throw Exception("Can't get rate max " + mDeviceName, -ret);
	}
}

void AlsaPcm::queryHwParamBuffer(snd_pcm_hw_params_t* hwParams,
								 SoundItf::PcmParamRanges& req,
								 SoundItf::PcmParamRanges& resp)

{
	snd_pcm_uframes_t minFrames = static_cast<snd_pcm_uframes_t>(req.buffer.min);
	snd_pcm_uframes_t maxFrames = static_cast<snd_pcm_uframes_t>(req.buffer.max);
	int ret;

	if ((ret = snd_pcm_hw_params_set_buffer_size_minmax(mHwQueryHandle, hwParams,
			&minFrames, &maxFrames)) < 0)
	{
		/*
		 * This is not really a fatal error, the frontend just tries to
		 * reduce configuration space.
		 * But, anyway, throw now, so we can return error code to
		 * the frontend.
		 */
		throw;
	}

	if ((ret = snd_pcm_hw_params_get_buffer_size_min(hwParams, &minFrames)) < 0)
	{
		throw Exception("Can't get buffer min" + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_hw_params_get_buffer_size_max(hwParams, &maxFrames)) < 0)
	{
		throw Exception("Can't get buffer max" + mDeviceName, -ret);
	}

	resp.buffer.min = static_cast<unsigned int>(minFrames);
	resp.buffer.max = static_cast<unsigned int>(maxFrames);
}

void AlsaPcm::queryHwParamChannels(snd_pcm_hw_params_t* hwParams,
								   SoundItf::PcmParamRanges& req,
								   SoundItf::PcmParamRanges& resp)
{
	int ret;

	if ((ret = snd_pcm_hw_params_set_channels_minmax(mHwQueryHandle, hwParams,
			&req.channels.min, &req.channels.max)) < 0)
	{
		/*
		 * This is not really a fatal error, the frontend just tries to
		 * reduce configuration space.
		 * But, anyway, throw now, so we can return error code to
		 * the frontend.
		 */
	}

	if ((ret = snd_pcm_hw_params_get_channels_min(hwParams, &resp.channels.min)) < 0)
	{
		throw Exception("Can't get channels min " + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_hw_params_get_channels_max(hwParams, &resp.channels.max)) < 0)
	{
		throw Exception("Can't get channels max " + mDeviceName, -ret);
	}
}

void AlsaPcm::queryHwParamPeriod(snd_pcm_hw_params_t* hwParams,
								 SoundItf::PcmParamRanges& req,
								 SoundItf::PcmParamRanges& resp)
{
	snd_pcm_uframes_t minFrames = static_cast<snd_pcm_uframes_t>(req.period.min);
	snd_pcm_uframes_t maxFrames = static_cast<snd_pcm_uframes_t>(req.period.max);
	int ret;

	if ((ret = snd_pcm_hw_params_set_period_size_minmax(mHwQueryHandle, hwParams,
			&minFrames, 0, &maxFrames, 0)) < 0)
	{
		/*
		 * This is not really a fatal error, the frontend just tries to
		 * reduce configuration space.
		 * But, anyway, throw now, so we can return error code to
		 * the frontend.
		 */
		throw;
	}

	if ((ret = snd_pcm_hw_params_get_period_size_min(hwParams, &minFrames, 0)) < 0)
	{
		throw Exception("Can't get period min" + mDeviceName, -ret);
	}

	if ((ret = snd_pcm_hw_params_get_period_size_max(hwParams, &maxFrames, 0)) < 0)
	{
		throw Exception("Can't get period max" + mDeviceName, -ret);
	}

	resp.period.min = static_cast<unsigned int>(minFrames);
	resp.period.max = static_cast<unsigned int>(maxFrames);
}

void AlsaPcm::queryHwParamFormats(snd_pcm_hw_params_t* hwParams,
								  SoundItf::PcmParamRanges& req,
								  SoundItf::PcmParamRanges& resp)
{
	snd_pcm_format_mask_t* alsa_formats;
	int ret;

	snd_pcm_format_mask_alloca(&alsa_formats);
	snd_pcm_format_mask_none(alsa_formats);

	for (auto value : sPcmFormat)
	{
		if (1 << value.sndif & req.formats)
		{
			snd_pcm_format_mask_set(alsa_formats, value.alsa);
		}
	}

	if ((ret = snd_pcm_hw_params_set_format_mask(mHwQueryHandle, hwParams,
			alsa_formats)) < 0)
	{
		/*
		 * This is not really a fatal error, the frontend just tries to
		 * reduce configuration space.
		 * But, anyway, throw now, so we can return error code to
		 * the frontend.
		 */
		throw;
	}

	snd_pcm_hw_params_get_format_mask(hwParams, alsa_formats);

	resp.formats = 0;

	for (auto value : sPcmFormat)
	{
		if (snd_pcm_format_mask_test(alsa_formats, value.alsa))
		{
			resp.formats |= 1 << value.sndif;
		}
	}
}

}

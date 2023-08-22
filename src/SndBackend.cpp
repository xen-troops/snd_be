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

#include "SndBackend.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include <csignal>
#include <execinfo.h>
#include <getopt.h>

#include <xen/errno.h>
#include <xen/be/XenStore.hpp>

#ifdef WITH_MOCKBELIB
#include "MockBackend.hpp"
#endif

#include "Version.hpp"

/***************************************************************************//**
 * @mainpage snd_be
 *
 * This backend implements virtual sound devices. It is implemented with
 * libxenbe.
 *
 ******************************************************************************/

using std::cout;
using std::endl;
using std::exception;
using std::ofstream;
using std::replace;
using std::signal;
using std::string;
using std::to_string;
using std::transform;
using std::unique_ptr;
using std::vector;

using XenBackend::FrontendHandlerException;
using XenBackend::FrontendHandlerPtr;
using XenBackend::Log;
using XenBackend::RingBufferPtr;
using XenBackend::RingBufferInBase;
using XenBackend::Utils;
using XenBackend::XenStore;

using SoundItf::StreamType;
using SoundItf::PcmDevicePtr;
using SoundItf::PcmType;

#ifdef WITH_PULSE
using Pulse::PulseMainloop;
#endif

#ifdef WITH_PIPEWIRE
using PipeWire::PipeWireMainloop;
#endif

string gLogFileName;

/*******************************************************************************
 * StreamRingBuffer
 ******************************************************************************/

StreamRingBuffer::StreamRingBuffer(const string& id, PcmDevicePtr pcmDevice,
								   EventRingBufferPtr eventRingBuffer,
								   domid_t domId, evtchn_port_t port,
								   grant_ref_t ref) :
	RingBufferInBase<xen_sndif_back_ring, xen_sndif_sring,
					 xensnd_req, xensnd_resp>(domId, port, ref),
	mId(id),
	mCommandHandler(pcmDevice, eventRingBuffer, domId),
	mLog("StreamRing")
{
	LOG(mLog, DEBUG) << "Create stream ring buffer, id: " << id;
}

void StreamRingBuffer::processRequest(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Request received, id: " << mId
					  << ", cmd:" << static_cast<int>(req.operation);

	xensnd_resp rsp {};

	rsp.id = req.id;
	rsp.operation = req.operation;
	rsp.status = mCommandHandler.processCommand(req, rsp);

	sendResponse(rsp);
}

/*******************************************************************************
 * SndFrontendHandler
 ******************************************************************************/
SndFrontendHandler::SndFrontendHandler(const string devName,
									   domid_t domId, uint16_t devId) :
	FrontendHandlerBase("SndFrontend", devName, domId, devId),
#ifdef WITH_PULSE
	mPulseMainloop("Dom" + to_string(domId) + ":" + to_string(devId)),
#endif
#ifdef WITH_PIPEWIRE
	mPipeWireMainloop("Dom" + to_string(domId) + ":" + to_string(devId)),
#endif
	mLog("SndFrontend")
{
}

void SndFrontendHandler::onBind()
{
	LOG(mLog, DEBUG) << "onBind";

	processCard(getXsFrontendPath() + "/");
}

void SndFrontendHandler::onClosing()
{
	LOG(mLog, DEBUG) << "onClosing";
}

void SndFrontendHandler::processCard(const std::string& cardPath)
{
	int devIndex = 0;

	while(getXenStore().checkIfExist(cardPath + to_string(devIndex)))
	{
		LOG(mLog, DEBUG) << "Found device: " << devIndex;

		processDevice(cardPath + to_string(devIndex) + "/");

		devIndex++;
	}
}

void SndFrontendHandler::processDevice(const std::string& devPath)
{
	int streamIndex = 0;

	while(getXenStore().checkIfExist(devPath + to_string(streamIndex)))
	{
		LOG(mLog, DEBUG) << "Found stream: " << streamIndex;

		processStream(devPath + to_string(streamIndex) + "/");

		streamIndex++;
	}
}

void SndFrontendHandler::processStream(const std::string& streamPath)
{
	auto id = getXenStore().readString(streamPath +
									   XENSND_FIELD_STREAM_UNIQUE_ID);
	StreamType streamType = StreamType::PLAYBACK;

	if (getXenStore().readString(streamPath + XENSND_FIELD_TYPE) ==
		XENSND_STREAM_TYPE_CAPTURE)
	{
		streamType = StreamType::CAPTURE;
	}

	createStream(id, streamType, streamPath);
}

void SndFrontendHandler::createStream(const string& id, StreamType type,
									  const string& streamPath)
{
	auto reqPort = getXenStore().readInt(streamPath +
										 XENSND_FIELD_EVT_CHNL);

	uint32_t reqRef = getXenStore().readInt(streamPath +
											XENSND_FIELD_RING_REF);

	auto evtPort = getXenStore().readInt(streamPath +
										 XENSND_FIELD_EVT_EVT_CHNL);

	uint32_t evtRef = getXenStore().readInt(streamPath +
											XENSND_FIELD_EVT_RING_REF);

	EventRingBufferPtr evtRingBuffer(new EventRingBuffer(
			getDomId(), evtPort, evtRef, XENSND_IN_RING_OFFS,
			XENSND_IN_RING_SIZE));

	addRingBuffer(evtRingBuffer);

	RingBufferPtr reqRingBuffer(
			new StreamRingBuffer(id, createPcmDevice(type, id),
								 evtRingBuffer, getDomId(),
								 reqPort, reqRef));

	addRingBuffer(reqRingBuffer);
}

PcmDevicePtr SndFrontendHandler::createPcmDevice(StreamType type,
												 const string& id)
{
	string pcmType;;
	string deviceName;
	string propName;
	string propValue;

	parseStreamId(id, pcmType, deviceName, propName, propValue);

	transform(pcmType.begin(), pcmType.end(), pcmType.begin(),
			  (int (*)(int))toupper);

	PcmDevicePtr pcmDevice;

	LOG(mLog, DEBUG) << "Create pcm device, type: " << pcmType
					 << ", device: " << deviceName
					 << ", propName: " << propName
					 << ", propValue: " << propValue;

#ifdef WITH_PIPEWIRE
	if (pcmType == "PIPEWIRE" || pcmType.empty())
	{
		if (propName.empty())
		{
			propName = "media.role";
		}

		pcmDevice.reset(mPipeWireMainloop.createStream(type, id,
													propName, propValue,
													deviceName));
	}
#endif

#ifdef WITH_PULSE
	if (pcmType == "PULSE" || pcmType.empty())
	{
		if (propName.empty())
		{
			propName = "media.role";
		}

		pcmDevice.reset(mPulseMainloop.createStream(type, id,
													propName, propValue,
													deviceName));
	}
#endif

#ifdef WITH_ALSA
	if (pcmType == "ALSA" || pcmType.empty())
	{
		if (deviceName.empty())
		{
			deviceName = "default";
		}

		pcmDevice.reset(new Alsa::AlsaPcm(type, deviceName));
	}
#endif

	if (!pcmDevice)
	{
		throw FrontendHandlerException("Invalid PCM type: " + pcmType, EINVAL);
	}

	return pcmDevice;
}

void SndFrontendHandler::parseStreamId(const string& id,
									   string& pcmType, string& deviceName,
									   string& propName, string& propValue)
{
	LOG(mLog, DEBUG) << "Parse stream id: " << id;

	string input = id;

	pcmType = parsePcmType(input);
	deviceName = parseDeviceName(input);
	propName = parsePropName(input);
	propValue = parsePropValue(input);
}

string SndFrontendHandler::parsePcmType(string& input)
{
	if (input.empty())
	{
		return string();
	}

	auto pos = input.find("<");
	auto type = input.substr(0, pos);

	input.erase(0, pos);

	return type;
}

string SndFrontendHandler::parseDeviceName(string& input)
{
	if (input.empty())
	{
		return string();
	}

	auto pos = input.find(">");

	if (pos == string::npos)
	{
		throw FrontendHandlerException("Can't get device name from id: " + input,
									   EINVAL);
	}

	auto device = input.substr(1, pos - 1);

	input.erase(0, ++pos);

	replace(device.begin(), device.end(), ';', ',');

	return device;
}

string SndFrontendHandler::parsePropName(string& input)
{
	if (input.empty())
	{
		return string();
	}

	auto pos = input.find(":");

	if (pos == string::npos)
	{
		return input;
	}

	auto propName = input.substr(0, pos);

	input.erase(0, ++pos);

	return propName;
}

string SndFrontendHandler::parsePropValue(string& input)
{
	if (input.empty())
	{
		return string();
	}

	return input;
}

/*******************************************************************************
 * SndBackend
 ******************************************************************************/

void SndBackend::onNewFrontend(domid_t domId, uint16_t devId)
{
	addFrontendHandler(FrontendHandlerPtr(new SndFrontendHandler(
			getDeviceName(), domId, devId)));
}

/*******************************************************************************
 *
 ******************************************************************************/

void segmentationHandler(int sig)
{
	void *array[20];
	size_t size;

	LOG("Main", ERROR) << "Segmentation fault!";

	size = backtrace(array, 2);

	backtrace_symbols_fd(array, size, STDERR_FILENO);

	raise(sig);
}

void registerSignals()
{
	struct sigaction act {};

	act.sa_handler = segmentationHandler;
	act.sa_flags = SA_RESETHAND;

	sigaction(SIGSEGV, &act, nullptr);
}

void waitSignals()
{
	sigset_t set;
	int signal;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigprocmask(SIG_BLOCK, &set, nullptr);

	sigwait(&set,&signal);
}

bool commandLineOptions(int argc, char *argv[])
{
	int opt = -1;

	while((opt = getopt(argc, argv, "c:v:l:fh?")) != -1)
	{
		switch(opt)
		{
		case 'v':

			if (!Log::setLogMask(string(optarg)))
			{
				return false;
			}

			break;

		case 'l':

			gLogFileName = optarg;

			break;

		case 'f':

			Log::setShowFileAndLine(true);

			break;

		default:

			return false;
		}
	}

	return true;
}

int main(int argc, char *argv[])
{
	try
	{
		registerSignals();

		if (commandLineOptions(argc, argv))
		{
			LOG("Main", INFO) << "backend version:  " << VERSION;
			LOG("Main", INFO) << "libxenbe version: " << Utils::getVersion();

			ofstream logFile;

			if (!gLogFileName.empty())
			{
				logFile.open(gLogFileName);
				Log::setStreamBuffer(logFile.rdbuf());
			}

#ifdef WITH_MOCKBELIB
			MockBackend mockBackend(0, 1);
#endif

			SndBackend sndBackend(XENSND_DRIVER_NAME);

			sndBackend.start();

			waitSignals();

			sndBackend.stop();

			logFile.close();
		}
		else
		{
			cout << "Usage: " << argv[0]
				 << " [-l <file>] [-v <level>]"
				 << endl;
			cout << "\t-l -- log file" << endl;
			cout << "\t-v -- verbose level in format: "
				 << "<module>:<level>;<module:<level>" << endl;
			cout << "\t      use * for mask selection:"
				 << " *:Debug,Mod*:Info" << endl;
		}
	}
	catch(const exception& e)
	{
		LOG("Main", ERROR) << e.what();
	}
	catch(...)
	{
		LOG("Main", ERROR) << "Unknown error";
	}

	return 0;
}

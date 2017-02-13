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

#include <iostream>
#include <memory>
#include <vector>

#include <csignal>
#include <execinfo.h>
#include <getopt.h>

#include <xen/be/XenStore.hpp>

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
using std::signal;
using std::string;
using std::to_string;
using std::unique_ptr;
using std::vector;

using XenBackend::FrontendHandlerPtr;
using XenBackend::Log;
using XenBackend::RingBufferPtr;
using XenBackend::RingBufferInBase;
using XenBackend::XenStore;

/***************************************************************************//**
 * StreamRingBuffer
 ******************************************************************************/

StreamRingBuffer::StreamRingBuffer(int id, Alsa::StreamType type, domid_t domId,
								   evtchn_port_t port, grant_ref_t ref) :
	RingBufferInBase<xen_sndif_back_ring, xen_sndif_sring,
					 xensnd_req, xensnd_resp>(domId, port, ref),
	mId(id),
	mCommandHandler(type, domId),
	mLog("StreamRing(" + to_string(id) + ")")
{
	LOG(mLog, DEBUG) << "Create stream ring buffer: id = " << id
					 << ", type:" << static_cast<int>(type);
}

void StreamRingBuffer::processRequest(const xensnd_req& req)
{
	DLOG(mLog, DEBUG) << "Request received, id: " << mId
					  << ", cmd:" << static_cast<int>(req.operation);

	xensnd_resp rsp {};

	rsp.id = req.id;
	rsp.operation = req.operation;
	rsp.status = mCommandHandler.processCommand(req);

	sendResponse(rsp);
}

/***************************************************************************//**
 * SndFrontendHandler
 ******************************************************************************/

void SndFrontendHandler::onBind()
{
	processCard(getXsFrontendPath() + "/");
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
	int id = getXenStore().readInt(streamPath + XENSND_FIELD_STREAM_UNIQUE_ID);
	Alsa::StreamType streamType = Alsa::StreamType::PLAYBACK;

	if (getXenStore().readString(streamPath + XENSND_FIELD_TYPE) ==
		XENSND_STREAM_TYPE_CAPTURE)
	{
		streamType = Alsa::StreamType::CAPTURE;
	}

	createStream(id, streamType, streamPath);
}

void SndFrontendHandler::createStream(int id, Alsa::StreamType type,
									  const string& streamPath)
{
	auto port = getXenStore().readInt(streamPath + XENSND_FIELD_EVT_CHNL);

	LOG(mLog, DEBUG) << "Read event channel port: " << port
					 << ", dom: " << getDomId();

	uint32_t ref = getXenStore().readInt(streamPath + XENSND_FIELD_RING_REF);

	LOG(mLog, DEBUG) << "Read ring buffer ref: " << ref
					 << ", dom: " << getDomId();

	RingBufferPtr ringBuffer(
			new StreamRingBuffer(id, type, getDomId(), port, ref));

	addRingBuffer(ringBuffer);
}

/***************************************************************************//**
 * SndBackend
 ******************************************************************************/

void SndBackend::onNewFrontend(domid_t domId, int id)
{
	addFrontendHandler(FrontendHandlerPtr(
					   new SndFrontendHandler(*this, domId, id)));
}

/***************************************************************************//**
 *
 ******************************************************************************/

void segmentationHandler(int sig)
{
	void *array[20];
	size_t size;

	LOG("Main", ERROR) << "Segmentation fault!";

	size = backtrace(array, 2);

	backtrace_symbols_fd(array, size, STDERR_FILENO);

	exit(1);
}

void registerSignals()
{
	signal(SIGSEGV, segmentationHandler);
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

	while((opt = getopt(argc, argv, "v:fh?")) != -1)
	{
		switch(opt)
		{
		case 'v':
			if (!Log::setLogLevel(string(optarg)))
			{
				return false;
			}

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
			SndBackend sndBackend("SndBackend", XENSND_DRIVER_NAME, 0);

			sndBackend.start();

			waitSignals();
		}
		else
		{
			cout << "Usage: " << argv[0] << " [-v <level>]" << endl;
			cout << "\t-v -- verbose level "
				 << "(disable, error, warning, info, debug)" << endl;
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

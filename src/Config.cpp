/*
 *  Config
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

#include "Config.hpp"

#include <algorithm>

using std::string;
using std::to_string;
using std::transform;

using libconfig::Setting;
using libconfig::FileIOException;
using libconfig::ParseException;
using libconfig::SettingException;
using libconfig::SettingNotFoundException;

using SoundItf::PcmType;
using SoundItf::StreamType;

/*******************************************************************************
 * Config
 ******************************************************************************/

Config::Config(const string& fileName) :
	mLog("Config"),
	mPcmType(PcmType::ALSA)
{
	const char* cfgName = cDefaultCfgName;

	try
	{
		if (!fileName.empty())
		{
			cfgName = fileName.c_str();
		}

		LOG(mLog, DEBUG) << "Open file: " << cfgName;

		mConfig.readFile(cfgName);

		initCachedValues();
	}
	catch(const FileIOException& e)
	{
		LOG(mLog, WARNING) << "Can't open config file: " << cfgName
						   << ", default settings will be used.";
	}
	catch(const ParseException& e)
	{
		throw ConfigException("Config: " + string(e.getError()) +
							  ", file: " + string(e.getFile()) +
							  ", line: " + to_string(e.getLine()));
	}
}

/*******************************************************************************
 * Public
 ******************************************************************************/

string Config::getStreamDevice(StreamType type, uint32_t id)
{
	if (type == StreamType::PLAYBACK)
	{
		return readDevice("playbackStreams.streams", id,
						  mDefaultPlaybackDevice);
	}

	if (type == StreamType::CAPTURE)
	{
		return readDevice("captureStreams.streams", id,
						  mDefaultCaptureDevice);
	}

	throw ConfigException("Unknown stream type: " +
						  to_string(static_cast<int>(type)));
}

void Config::getStreamPropery(StreamType type, uint32_t id,
							  string& name, string& value)
{
	if (type == StreamType::PLAYBACK)
	{
		readProperty("playbackStreams.streams", id, name, value,
						  mDefaultPlaybackPropName);

		return;
	}

	if (type == StreamType::CAPTURE)
	{
		readProperty("captureStreams.streams", id, name, value,
						  mDefaultCapturePropName);

		return;
	}

	throw ConfigException("Unknown stream type: " +
						  to_string(static_cast<int>(type)));}

/*******************************************************************************
 * Private
 ******************************************************************************/

void Config::initCachedValues()
{
	mPcmType = readPcmType();
	mDefaultPlaybackDevice = readDefaultPlaybackDevice();
	mDefaultCaptureDevice = readDefaultCaptureDevice();
	mDefaultPlaybackPropName = readDefaultPlaybackPropName();
	mDefaultCapturePropName = readDefaultCapturePropName();
}

PcmType Config::readPcmType()
{
	string pcmType;

	if (mConfig.lookupValue("soundSystem", pcmType))
	{
		transform(pcmType.begin(), pcmType.end(), pcmType.begin(), ::toupper);

		if (pcmType == "PULSE")
		{
#ifndef WITH_PULSE
			throw ConfigException("Pulse PCM is not supported");
#endif
			return PcmType::PULSE;
		}
		else if (pcmType == "ALSA")
		{
#ifndef WITH_ALSA
			throw ConfigException("Alsa PCM is not supported");
#endif
			return PcmType::ALSA;
		}
		else
		{
			throw ConfigException("Wrong soundSystem: " + pcmType);
		}
	}

	LOG(mLog, DEBUG) << "soundSystem setting not found: default ALSA";

	return mPcmType;
}

string Config::readDefaultPlaybackDevice()
{
	string device;

	mConfig.lookupValue("playbackStreams.defaultDevice",
						device);

	return device;
}

string Config::readDefaultCaptureDevice()
{
	string device;

	mConfig.lookupValue("captureStreams.defaultDevice",
						device);

	return device;
}

string Config::readDefaultPlaybackPropName()
{
	string propName;

	mConfig.lookupValue("playbackStreams.defaultPropName",
						propName);

	return propName;
}

string Config::readDefaultCapturePropName()
{
	string propName;

	mConfig.lookupValue("captureStreams.defaultPropName",
						propName);

	return propName;
}

string Config::readDevice(const string& sectionName, uint32_t id,
						  const string& defaultValue)
{
	if (mConfig.exists(sectionName))
	{
		Setting& section = mConfig.lookup(sectionName);

		for (int i = 0; i < section.getLength(); i++)
		{
			if (static_cast<uint32_t>(section[i].lookup("id")) == id)
			{
				string device;

				if (section[i].lookupValue("device", device))
				{
					return device;
				}
			}
		}
	}

	return defaultValue;
}

void Config::readProperty(const string& sectionName, uint32_t id,
						  string& name, string& value,
						  const string& defaultPropName)
{
	Setting& section = mConfig.lookup(sectionName);

	for (int i = 0; i < section.getLength(); i++)
	{
		if (static_cast<uint32_t>(section[i].lookup("id")) == id)
		{
			if (!section[i].lookupValue("propName", name))
			{
				name = defaultPropName;
			}

			section[i].lookupValue("propValue", value);

			return;
		}
	}

	name = defaultPropName;
}

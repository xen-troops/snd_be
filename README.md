# Sound backend

## Dependencies:
### Required:
* libxenbe
### Optional:
* alsa
* pulse
* Doxygen

## How to configure
```
mkdir ${BUILD_DIR}
cd ${BUILD_DIR}
cmake ${PATH_TO_SOURCES} -D${VAR1}=${VAL1} -D{VAR2}=${VAL2} ...
```
There are variables and options. Options can be set to ON and OFF.

Supported options:

| Option | Description |
| --- | --- |
| `WITH_DOC` | Creates target to build documentation. It required Doxygen to be installed. If configured, documentation can be create with `make doc` |
| `WITH_PULSE` | Builds with pulse audio backend |
| `WITH_ALSA` | Builds with alsa backend |
| `WITH_MOCKBELIB` | Use test mock backend library |

Supported variables:

| Variable | Description |
| --- | --- |
| `CMAKE_BUILD_TYPE` | `Realease`, `Debug`, `RelWithDebInfo`, `MinSizeRel`|
| `CMAKE_INSTALL_PREFIX` | Default install path |
| `XEN_INCLUDE_PATH` | Path to Xen tools includes if they are located in non standard place |
| `XENBE_INCLUDE_PATH` | Path to libxenbe includes if they are located in non standard place |
| `IF_INCLUDE_PATH` | Path to the interface headers if they are located in non standard place |
| `XENBEMOCK_INCLUDE_PATH` | Path to the mock headers if they are located in non standard place |
| `XEN_LIB_PATH` | Path to Xen tools libraries if they are located in non standard place |
| `XENBE_LIB_PATH` | Path to libxenbe if it is located in non standard place |
| `XENBEMOCK_LIB_PATH` | Path to libxenbemock if it is located in non standard place |

Example:
```
// Debug build with pulse backend
cmake ${PATH_TO_SOURCES} -DWITH_PULSE=ON -DCMAKE_BUILD_TYPE=Debug
```

## How to build:
```
cd ${BUILD_DIR}
make     // build sources
make doc // build documentation
```
## How to install
```
make install // to default location
make DESTDIR=${PATH_TO_INSTALL} install //to other location
```
## Configuration:

Backend configuration is done in domain configuration file. See vsnd on http://xenbits.xen.org/docs/unstable-staging/man/xl.cfg.5.html#Devices

In order to match virtual stream with real one, stream unique-id parameter is used. This parameter has following format:

`pcmtype<device>propname:propvalue`

All fields except `pcmtype` are optional.

* `pcmtype` - specifies PCM type: "pulse" or "alsa";
* `device` - device name
    * for pulse: sink or source name
    * for alsa: alsa device like HW:0;1 (note that ";" used instead of "," because "," is field separator in domain config file)
* `propname` (relevant for pulse) - stream property name: like media.role etc.
* `propvalue` (relevant for pulse) - stream property value: like navi, phone etc.

Stream property is used to identify pulse stream by other system modules such as audio manager etc.

Some configuration examples:
```
vsnd = [[ 'card, backend=DomD, buffer-size=65536, short-name=VCard, long-name=Virtual sound card, sample-rates=48000, sample-formats=s16_le',
          'pcm, name=dev1', 'stream, unique-id=alsa, type=P' ]]
```
The backend will provide default alsa device for the configured stream playback.

Other unique-id values example:
```
# the backend will provide default pulse device for the configured stream
unique-id=pulse
# the backend will provide pulse pci-0000_00_1f.3.analog-stereo device for the configured stream
unique-id=pulse<pci-0000_00_1f.3.analog-stereo> 
# the backend will provide default pulse device for the configured stream and set media.role property of stream to navi
unique-id=pulse<>media.role:navi
# the backend will provide alsa card0 device 0 for the configured stream 
unique-id=alsa<hw:0;0>
```

## How to run:
```
snd_be -v${LOG_MASK}
```
Example:

```
snd_be -v *:Debug
```
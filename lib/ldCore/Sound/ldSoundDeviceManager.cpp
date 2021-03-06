/**
    libLaserdockCore
    Copyright(c) 2018 Wicked Lasers

    This file is part of libLaserdockCore.

    libLaserdockCore is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libLaserdockCore is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libLaserdockCore.  If not, see <https://www.gnu.org/licenses/>.
**/

#include "ldSoundDeviceManager.h"


#include <QtCore/QtDebug>
#include <QtCore/QCoreApplication>
#include <QtWidgets/QMessageBox>

#include <ldCore/Sound/ldLoopbackAudioDevice.h>

#include "ldAudioDecoder.h"
#include "ldQAudioDecoder.h"

#include "ldQAudioInputDevice.h"
// loopback
#ifdef LD_LOOPBACK_DEVICE_ENABLED
#include <ldCore/Sound/ldLoopbackAudioDeviceWorker.h>
#include "ldCore/Visualizations/MusicManager/ldMusicManager.h"
#endif
// stub
#include "ldSoundStubDevice.h"
// midi
#ifdef LD_CORE_ENABLE_MIDI
#include "ldMidiDevice.h"
#include "Midi/ldMidiInfo.h"
#include "Midi/ldMidiInput.h"
#endif

void ldSoundDeviceManager::registerMetaType()
{
#ifdef LD_CORE_ENABLE_MIDI
   ldMidiDevice::registerMetaType();
#endif
}

ldSoundDeviceManager::ldSoundDeviceManager(QObject *parent)
    : ldSoundInterface(parent)
    , m_isPriorityDevice(false)
    , m_qaudioInputDevice(new ldQAudioInputDevice(this))
#ifdef LD_CORE_ENABLE_MIDI
    , m_midiDevice(new ldMidiDevice(this))
#endif
    , m_stubDevice(new ldSoundStubDevice(this))
    , m_playerDevice(new ldAudioDecoder(this))
{
    connect(m_qaudioInputDevice, &ldQAudioInputDevice::soundUpdated, this, &ldSoundDeviceManager::soundUpdated);
    connect(m_qaudioInputDevice, &ldQAudioInputDevice::error, this, &ldSoundDeviceManager::error);
#ifdef LD_CORE_ENABLE_MIDI
    connect(m_midiDevice, &ldMidiDevice::soundUpdated, this, &ldSoundDeviceManager::soundUpdated);
#endif
    connect(m_stubDevice, &ldSoundStubDevice::soundUpdated, this, &ldSoundDeviceManager::soundUpdated);

    connect(m_playerDevice, &ldAudioDecoder::bufferUpdated, this, &ldSoundDeviceManager::processAudioBuffer);
    //
    refreshAvailableDevices();

    setDeviceInfo(getAvailableDevices(ldSoundDeviceInfo::Type::QAudioInput).last());
}

ldSoundDeviceManager::~ldSoundDeviceManager()
{
    deleteAudioInput();
}

void ldSoundDeviceManager::refreshAvailableDevices()
{
    m_devices.clear();

    QList<QAudioDeviceInfo> inputAudioDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    for(const QAudioDeviceInfo &inputAudioDevice : inputAudioDevices) {
        m_devices.append(ldSoundDeviceInfo(ldSoundDeviceInfo::Type::QAudioInput, inputAudioDevice.deviceName()));
    }

#ifdef LD_LOOPBACK_DEVICE_ENABLED
    QStringList loopbackDevices = ldLoopbackAudioDevice::getAvailableOutputDevices();
    for(const QString &loopbackDevice : loopbackDevices) {
        m_devices.append(ldSoundDeviceInfo(ldSoundDeviceInfo::Type::Loopback,
                                           loopbackDevice));
    }
#endif

#ifdef LD_CORE_ENABLE_MIDI
    // midi devices
    QList<ldMidiInfo> midiInfos = ldMidiInput::getDevices();
    for(const ldMidiInfo &midiInfo : midiInfos) {
        m_devices.append(ldSoundDeviceInfo(ldSoundDeviceInfo::Type::Midi,
                                           midiInfo.name(),
                                           QVariant::fromValue(midiInfo)));
    }
#endif

    m_devices.append(ldSoundDeviceInfo(ldSoundDeviceInfo::Type::Stub,
                                       tr("Stub"))
                     );
}

QList<ldSoundDeviceInfo> ldSoundDeviceManager::getAvailableDevices(const ldSoundDeviceInfo::Type &type) const
{
    QList<ldSoundDeviceInfo> result;
    for(const ldSoundDeviceInfo &device : m_devices) {
        if(type & device.type() || type == ldSoundDeviceInfo::Type::None) {
            result << device;
        }
    }
    return result;
}

ldSoundDeviceInfo ldSoundDeviceManager::getDeviceInfo() const
{
    return m_info;
}

void ldSoundDeviceManager::setDeviceInfo(const ldSoundDeviceInfo &info)
{
    if(m_info == info || m_priorityInfo.isValid()) {
        return;
    }

    if(initializeAudio(info)) {
        m_info = info;
    }
}

void ldSoundDeviceManager::setPriorityDevice(const ldSoundDeviceInfo &info)
{
    if(m_priorityInfo == info) {
        return;
    }

    if(info.isValid()) {
        // try to initialize priority device
        bool isOk = initializeAudio(info);
        if(isOk) {
            m_priorityInfo = info;
            update_isPriorityDevice(true);
        }
    } else {
        // restore old device
        update_isPriorityDevice(false);
        m_priorityInfo = info;
        initializeAudio(m_info);
    }
}

void ldSoundDeviceManager::setActivateCallbackFunc(ldActivateCallbackFunc func)
{
    m_activateCallbackFunc = func;
}

void ldSoundDeviceManager::notified()
{
    qDebug() << "notified";
}


#ifdef LD_LOOPBACK_DEVICE_ENABLED
void ldSoundDeviceManager::activateOutputDevice(ldSoundDeviceInfo info)
{
//    m_info = info;// info.deviceName();

    qDebug() << "using sound output device: " << info.name();

    // TODO: fill with correct loopback options
    m_format = getDefaultAudioFormat();

    deleteAudioInput();

    m_loopbackAudioDevice = new ldLoopbackAudioDevice(info.name(), this);
    connect(m_loopbackAudioDevice, SIGNAL(soundUpdated(const char*,qint64)), this, SIGNAL(soundUpdated(const char*,qint64)));
    connect(m_loopbackAudioDevice, &ldLoopbackAudioDevice::error, this, &ldSoundDeviceManager::error);
    m_loopbackAudioDevice->startCapture();
}
#endif

void ldSoundDeviceManager::deleteAudioInput()
{
    if(m_isStopping) return;

    m_isStopping = true;

    if(m_qaudioInputDevice->isActive()) {
        m_qaudioInputDevice->stop();
    }

#ifdef LD_LOOPBACK_DEVICE_ENABLED
    if(m_loopbackAudioDevice) {
        disconnect(m_loopbackAudioDevice, SIGNAL(soundUpdated(const char*,qint64)), this, SIGNAL(soundUpdated(const char*,qint64)));
        m_loopbackAudioDevice->stopCapture();
        m_loopbackAudioDevice->deleteLater();
        m_loopbackAudioDevice = NULL;
    }

#endif

#ifdef LD_CORE_ENABLE_MIDI
    m_midiDevice->stop();
#endif

    m_stubDevice->stop();
    m_playerDevice->stop();

    m_isStopping = false;
}

bool ldSoundDeviceManager::initializeAudio(const ldSoundDeviceInfo &info)
{
	deleteAudioInput();

	bool isSuccess = false;

	// find device
    switch (info.type()) {
    case ldSoundDeviceInfo::Type::QAudioInput:
    {
        isSuccess = activateQAudioInputDevice(info);
        break;
    }
    case ldSoundDeviceInfo::Type::Loopback:
    {
#ifdef LD_LOOPBACK_DEVICE_ENABLED
        // try look for loopback device
        if(ldLoopbackAudioDevice::getAvailableOutputDevices().contains(info.name())) {
            activateOutputDevice(info);
            isSuccess = true;
        }
#endif
        break;
    }
#ifdef LD_CORE_ENABLE_MIDI
    case ldSoundDeviceInfo::Type::Midi:
    {
        activateMidiDevice(info);
        isSuccess = true;
        break;
    }
#endif
    case ldSoundDeviceInfo::Type::Stub:
    {
        activateStubDevice(info);
        isSuccess = true;
        break;
    }
    case ldSoundDeviceInfo::Type::Decoder:
    {
        activatePlayerDevice(info);
        isSuccess = true;
        break;
    }
    case ldSoundDeviceInfo::Type::None:
    {
        isSuccess = true;
        break;
    }
    }

	if (!isSuccess) {
		// show error - nothing found
        qWarning() << "show error - nothing found";
        emit error(tr("Error, can't open: %1").arg(info.name()));
	}

    if(isSuccess && m_activateCallbackFunc) {
        m_activateCallbackFunc(info);
    }

    return isSuccess;
}

bool ldSoundDeviceManager::activateQAudioInputDevice(const ldSoundDeviceInfo &info)
{
    QList<QAudioDeviceInfo> inputDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);

    auto it = std::find_if(inputDevices.begin(), inputDevices.end(), [&](const QAudioDeviceInfo &inputDevice) {
        return inputDevice.deviceName() == info.name();
    });

    if (it != inputDevices.end()) {
        bool isOk = m_qaudioInputDevice->activateInputDevice(*it);
        if(isOk) {
            m_format = m_qaudioInputDevice->format();
        } else {
            deleteAudioInput();
        }
        return isOk;
    }

    return false;
}

#ifdef LD_CORE_ENABLE_MIDI
void ldSoundDeviceManager::activateMidiDevice(ldSoundDeviceInfo info) {

    // dummy options for pcm data
    m_format = getDefaultAudioFormat();
    m_midiDevice->start(info.id().value<ldMidiInfo>());

    qDebug() << "midi on";
}
#endif


void ldSoundDeviceManager::activatePlayerDevice(ldSoundDeviceInfo info)
{
    // dummy options for pcm data
    m_format = getDefaultAudioFormat();
    m_playerDevice->start(info.id().toString(), info.data().toInt());
}

void ldSoundDeviceManager::activateStubDevice(ldSoundDeviceInfo /*info*/)
{
    // dummy options for pcm data
    m_format = getDefaultAudioFormat();
    m_stubDevice->start();
}


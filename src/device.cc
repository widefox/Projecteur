// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "device.h"

#include "deviceinput.h"
#include "devicescan.h"
#include "hidpp.h"
#include "logging.h"

#include <QSocketNotifier>
#include <QTimer>

#include <fcntl.h>
#include <linux/hidraw.h>
#include <unistd.h>

LOGGING_CATEGORY(device, "device")
LOGGING_CATEGORY(hid, "HID")

namespace  {
  // -----------------------------------------------------------------------------------------------
  static const auto registeredComparator_ = QMetaType::registerComparators<DeviceId>();

  const auto hexId = logging::hexId;
  // class i18n : public QObject {}; // for i18n and logging
}

// -------------------------------------------------------------------------------------------------
DeviceConnection::DeviceConnection(const DeviceId& id, const QString& name,
                                   std::shared_ptr<VirtualDevice> vdev)
  : m_deviceId(id)
  , m_deviceName(name)
  , m_inputMapper(std::make_shared<InputMapper>(std::move(vdev)))
{
}

// -------------------------------------------------------------------------------------------------
DeviceConnection::~DeviceConnection() = default;

// -------------------------------------------------------------------------------------------------
bool DeviceConnection::hasSubDevice(const QString& path) const
{
  const auto find_it = m_subDeviceConnections.find(path);
  return (find_it != m_subDeviceConnections.end() && find_it->second && find_it->second->isConnected());
}

// -------------------------------------------------------------------------------------------------
void DeviceConnection::addSubDevice(std::shared_ptr<SubDeviceConnection> sdc)
{
  if (!sdc) return;

  const auto path = sdc->path();
  connect(&*sdc, &SubDeviceConnection::flagsChanged, this, [this, path](){
    emit subDeviceFlagsChanged(m_deviceId, path);
  });

  m_subDeviceConnections[path] = std::move(sdc);
  emit subDeviceConnected(m_deviceId, path);
}

// -------------------------------------------------------------------------------------------------
bool DeviceConnection::removeSubDevice(const QString& path)
{
  auto find_it = m_subDeviceConnections.find(path);
  if (find_it != m_subDeviceConnections.end())
  {
    if (find_it->second) { find_it->second->disconnect(); } // Important
    logDebug(device) << tr("Disconnected sub-device: %1 (%2:%3) %4")
                        .arg(m_deviceName, hexId(m_deviceId.vendorId),
                             hexId(m_deviceId.productId), path);
    emit subDeviceDisconnected(m_deviceId, path);
    m_subDeviceConnections.erase(find_it);
    return true;
  }
  return false;
}


// -------------------------------------------------------------------------------------------------
bool DeviceConnection::hasHidppSupport() const {
  // HID++ only for Logitech devices
  return m_deviceId.vendorId == 0x046d;
}

// // -------------------------------------------------------------------------------------------------
// void DeviceConnection::queryBatteryStatus()
// {
//   for (const auto& sd: subDevices())
//   {
//     if (sd.second->type() == ConnectionType::Hidraw
//         && sd.second->mode() == ConnectionMode::ReadWrite)
//     {
//       if (sd.second->hasFlags(DeviceFlag::ReportBattery)) sd.second->queryBatteryStatus();
//     }
//   }
// }

// // -------------------------------------------------------------------------------------------------
// void DeviceConnection::setBatteryInfo(const QByteArray& batteryData)
// {
//   // TODO Refactor battery handling
//   const bool hasBattery =
//     std::any_of(m_subDeviceConnections.cbegin(), m_subDeviceConnections.cend(), [](const auto& sd) {
//       return sd.second->hasFlags(DeviceFlag::ReportBattery);
//     });

//   if (hasBattery && batteryData.length() == 3)
//   {
//     // Battery percent is only meaningful when battery is discharging. However, save them anyway.
//     m_batteryInfo.currentLevel
//       = static_cast<uint8_t>(batteryData.at(0) <= 100 ? batteryData.at(0) : 100);
//     m_batteryInfo.nextReportedLevel
//       = static_cast<uint8_t>(batteryData.at(1) <= 100 ? batteryData.at(1): 100);
//     m_batteryInfo.status = static_cast<BatteryStatus>((batteryData.at(2) <= 0x07) ? batteryData.at(2): 0x07);
//   }
// }

// -------------------------------------------------------------------------------------------------
SubDeviceConnectionDetails::SubDeviceConnectionDetails(const DeviceScan::SubDevice& sd,
                                                       ConnectionType type, ConnectionMode mode)
  : type(type), mode(mode), devicePath(sd.deviceFile)
{}

// -------------------------------------------------------------------------------------------------
SubDeviceConnection::SubDeviceConnection(const DeviceScan::SubDevice& sd,
                                         ConnectionType type, ConnectionMode mode)
  : m_details(sd, type, mode) {}

// -------------------------------------------------------------------------------------------------
SubDeviceConnection::~SubDeviceConnection() = default;

// -------------------------------------------------------------------------------------------------
DeviceFlags SubDeviceConnection::setFlags(DeviceFlags f, bool set)
{
  const auto previousFlags = flags();
  if (set) {
    m_details.deviceFlags |= f;
  } else {
    m_details.deviceFlags &= ~f;
  }

  if (m_details.deviceFlags != previousFlags) {
    emit flagsChanged(m_details.deviceFlags);
  }
  return m_details.deviceFlags;
}

// -------------------------------------------------------------------------------------------------
bool SubDeviceConnection::isConnected() const {
  return false;
}

// -------------------------------------------------------------------------------------------------
void SubDeviceConnection::disconnect() {
  if (m_readNotifier) {
    m_readNotifier->setEnabled(false);
    m_readNotifier.reset();
  }
}

// -------------------------------------------------------------------------------------------------
void SubDeviceConnection::setReadNotifierEnabled(bool enabled) {
  if (m_readNotifier) m_readNotifier->setEnabled(enabled);
}

// -------------------------------------------------------------------------------------------------
const std::shared_ptr<InputMapper>& SubDeviceConnection::inputMapper() const  {
  return m_inputMapper;
}

// -------------------------------------------------------------------------------------------------
QSocketNotifier* SubDeviceConnection::socketReadNotifier() {
  return m_readNotifier.get();
}

// -------------------------------------------------------------------------------------------------
SubEventConnection::SubEventConnection(Token, const DeviceScan::SubDevice& sd)
  : SubDeviceConnection(sd, ConnectionType::Event, ConnectionMode::ReadOnly) {}

// -------------------------------------------------------------------------------------------------
bool SubEventConnection::isConnected() const {
  return (m_readNotifier && m_readNotifier->isEnabled());
}

// -------------------------------------------------------------------------------------------------
std::shared_ptr<SubEventConnection> SubEventConnection::create(const DeviceScan::SubDevice& sd,
                                                               const DeviceConnection& dc)
{
  const int evfd = ::open(sd.deviceFile.toLocal8Bit().constData(), O_RDONLY, 0);

  if (evfd == -1) {
    logWarn(device) << tr("Cannot open event device '%1' for read.").arg(sd.deviceFile);
    return std::shared_ptr<SubEventConnection>();
  }

  struct input_id id{};
  ioctl(evfd, EVIOCGID, &id); // get the event sub-device id

  // Check against given device id
  if (id.vendor != dc.deviceId().vendorId || id.product != dc.deviceId().productId)
  {
    ::close(evfd);
    logDebug(device) << tr("Device id mismatch: %1 (%2:%3)")
                        .arg(sd.deviceFile, hexId(id.vendor), hexId(id.product));
    return std::shared_ptr<SubEventConnection>();
  }

  unsigned long bitmask = 0;
  if (ioctl(evfd, EVIOCGBIT(0, sizeof(bitmask)), &bitmask) < 0)
  {
    ::close(evfd);
    logWarn(device) << tr("Cannot get device properties: %1 (%2:%3)")
                       .arg(sd.deviceFile, hexId(id.vendor), hexId(id.product));
    return std::shared_ptr<SubEventConnection>();
  }

  auto connection = std::make_shared<SubEventConnection>(Token{}, sd);

  if (!!(bitmask & (1 << EV_SYN))) connection->m_details.deviceFlags |= DeviceFlag::SynEvents;
  if (!!(bitmask & (1 << EV_REP))) connection->m_details.deviceFlags |= DeviceFlag::RepEvents;
  if (!!(bitmask & (1 << EV_KEY))) connection->m_details.deviceFlags |= DeviceFlag::KeyEvents;
  if (!!(bitmask & (1 << EV_REL)))
  {
    unsigned long relEvents = 0;
    ioctl(evfd, EVIOCGBIT(EV_REL, sizeof(relEvents)), &relEvents);
    const bool hasRelXEvents = !!(relEvents & (1 << REL_X));
    const bool hasRelYEvents = !!(relEvents & (1 << REL_Y));
    if (hasRelXEvents && hasRelYEvents) {
      connection->m_details.deviceFlags |= DeviceFlag::RelativeEvents;
    }
  }

  connection->m_details.grabbed = [&dc, evfd, &sd]()
  {
    // Grab device inputs if a virtual device exists.
    if (dc.inputMapper()->virtualDevice())
    {
      const int res = ioctl(evfd, EVIOCGRAB, 1);
      if (res == 0) { return true; }

      // Grab not successful
      logError(device) << tr("Error grabbing device: %1 (return value: %2)").arg(sd.deviceFile).arg(res);
      ioctl(evfd, EVIOCGRAB, 0);
    }
    return false;
  }();

  fcntl(evfd, F_SETFL, fcntl(evfd, F_GETFL, 0) | O_NONBLOCK);
  if ((fcntl(evfd, F_GETFL, 0) & O_NONBLOCK) == O_NONBLOCK) {
    connection->m_details.deviceFlags |= DeviceFlag::NonBlocking;
  }

  // Create socket notifier
  connection->m_readNotifier = std::make_unique<QSocketNotifier>(evfd, QSocketNotifier::Read);
  QSocketNotifier* const notifier = connection->m_readNotifier.get();
  // Auto clean up and close descriptor on destruction of notifier
  connect(notifier, &QSocketNotifier::destroyed, [grabbed = connection->m_details.grabbed, notifier]() {
    if (grabbed) {
      ioctl(static_cast<int>(notifier->socket()), EVIOCGRAB, 0);
    }
    ::close(static_cast<int>(notifier->socket()));
  });

  connection->m_inputMapper = dc.inputMapper();
  return connection;
}

// -------------------------------------------------------------------------------------------------
SubHidrawConnection::SubHidrawConnection(Token, const DeviceScan::SubDevice& sd)
  : SubDeviceConnection(sd, ConnectionType::Hidraw, ConnectionMode::ReadWrite) {}

// -------------------------------------------------------------------------------------------------
SubHidrawConnection::~SubHidrawConnection() = default;

// -------------------------------------------------------------------------------------------------
bool SubHidrawConnection::isConnected() const {
  return (m_readNotifier && m_readNotifier->isEnabled()) && (m_writeNotifier);
}

// -------------------------------------------------------------------------------------------------
void SubHidrawConnection::disconnect() {
  SubDeviceConnection::disconnect();
  if (m_writeNotifier) {
    m_writeNotifier->setEnabled(false);
    m_writeNotifier.reset();
  }
}

// -------------------------------------------------------------------------------------------------
std::shared_ptr<SubHidrawConnection> SubHidrawConnection::create(const DeviceScan::SubDevice& sd,
                                                                 const DeviceConnection& dc)
{
  const int devfd = openHidrawSubDevice(sd, dc.deviceId());
  if (devfd == -1) return std::shared_ptr<SubHidrawConnection>();

  auto connection = std::make_shared<SubHidrawConnection>(Token{}, sd);
  connection->createSocketNotifiers(devfd);

  connect(connection->socketReadNotifier(), &QSocketNotifier::activated,
          &*connection, &SubHidrawConnection::onHidrawDataAvailable);

  return connection;
}

// -----------------------------------------------------------------------------------------------
int SubHidrawConnection::openHidrawSubDevice(const DeviceScan::SubDevice& sd, const DeviceId& devId)
{
  constexpr int errorResult = -1;
  const int devfd = ::open(sd.deviceFile.toLocal8Bit().constData(), O_RDWR|O_NONBLOCK , 0);

  if (devfd == errorResult) {
    logWarn(device) << tr("Cannot open hidraw device '%1' for read/write.").arg(sd.deviceFile);
    return errorResult;
  }

  { // Get Report Descriptor Size and Descriptor -- currently unused, but if it fails
    // we don't use the device
    int descriptorSize = 0;
    if (ioctl(devfd, HIDIOCGRDESCSIZE, &descriptorSize) < 0)
    {
      logWarn(device) << tr("Cannot retrieve report descriptor size of hidraw device '%1'.").arg(sd.deviceFile);
      ::close(devfd);
      return errorResult;
    }

    struct hidraw_report_descriptor reportDescriptor {};
    reportDescriptor.size = descriptorSize;
    if (ioctl(devfd, HIDIOCGRDESC, &reportDescriptor) < 0)
    {
      logWarn(device) << tr("Cannot retrieve report descriptor of hidraw device '%1'.").arg(sd.deviceFile);
      ::close(devfd);
      return errorResult;
    }
  }

  struct hidraw_devinfo devinfo {};
  // get the hidraw sub-device id info
  if (ioctl(devfd, HIDIOCGRAWINFO, &devinfo) < 0)
  {
    logWarn(device) << tr("Cannot get info from hidraw device '%1'.").arg(sd.deviceFile);
    ::close(devfd);
    return errorResult;
  };

  // Check against given device id
  if (static_cast<unsigned short>(devinfo.vendor) != devId.vendorId
      || static_cast<unsigned short>(devinfo.product) != devId.productId)
  {
    logDebug(device) << tr("Device id mismatch: %1 (%2:%3)")
                          .arg(sd.deviceFile, hexId(devinfo.vendor), hexId(devinfo.product));
    ::close(devfd);
    return errorResult;
  }

  return devfd;
}

// -------------------------------------------------------------------------------------------------
ssize_t SubHidrawConnection::sendData(const QByteArray& msg)
{
  return sendData(msg.data(), msg.size());
}

// -------------------------------------------------------------------------------------------------
ssize_t SubHidrawConnection::sendData(const void* msg, size_t msgLen)
{
  constexpr ssize_t errorResult = -1;

  if (mode() != ConnectionMode::ReadWrite || !m_writeNotifier) { return errorResult; }
  // TODO check against m_writeNotifier?
  const auto res = ::write(m_writeNotifier->socket(), msg, msgLen);

  if (static_cast<size_t>(res) == msgLen) {
    logDebug(hid) << res << "bytes written to" << path() << "("
                  << QByteArray::fromRawData(static_cast<const char*>(msg), msgLen).toHex() << ")";
  } else {
    logWarn(hid) << tr("Writing to '%1' failed. (%2)").arg(path()).arg(res);
  }

  return res;
}

// -------------------------------------------------------------------------------------------------
void SubHidrawConnection::createSocketNotifiers(int fd)
{
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  if ((fcntl(fd, F_GETFL, 0) & O_NONBLOCK) == O_NONBLOCK) {
    m_details.deviceFlags |= DeviceFlag::NonBlocking;
  }

  // Create read and write socket notifiers
  m_readNotifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
  QSocketNotifier *const readNotifier = m_readNotifier.get();
  // Auto clean up and close descriptor on destruction of notifier
  connect(readNotifier, &QSocketNotifier::destroyed, [readNotifier]() {
    ::close(static_cast<int>(readNotifier->socket()));
  });

  m_writeNotifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Write);
  QSocketNotifier *const writeNotifier = m_writeNotifier.get();
  writeNotifier->setEnabled(false); // Disable write notifier by default
  // Auto clean up and close descriptor on destruction of notifier
  connect(writeNotifier, &QSocketNotifier::destroyed, [writeNotifier]() {
    ::close(static_cast<int>(writeNotifier->socket()));
  });
}

// -------------------------------------------------------------------------------------------------
void SubHidrawConnection::onHidrawDataAvailable(int fd)
{
  QByteArray readVal(20, 0);
  const auto res = ::read(fd, readVal.data(), readVal.size());
  if (res < 0) {
    if (errno != EAGAIN) {
      emit socketReadError(errno);
    }
    return;
  }

  // For generic hidraw devices without known protocols, just print out the
  // received data into the debug log
  logDebug(hid) << "Received" << readVal.toHex() << "from" << path();
}

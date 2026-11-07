#include "app/preview/webcam_recorder.h"

#include <QAudioBufferInput>
#include <QCamera>
#include <QFile>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QMutexLocker>
#include <QScopeGuard>
#include <QTimer>
#include <QUrl>
#include <QVideoFrameInput>
#include <QVideoSink>

WebcamRecorder::WebcamRecorder(QObject *parent) : QObject(parent) {}
WebcamRecorder::~WebcamRecorder() {
  if (m_recorder)
    m_recorder->stop();
  if (m_camera)
    m_camera->stop();
}

void WebcamRecorder::ensureCamera() {
  if (m_camera)
    return;
  m_camera = new QCamera(this);
  m_preview = new QMediaCaptureSession(this);
  m_cameraSink = new QVideoSink(this);
  m_preview->setCamera(m_camera);
  m_preview->setVideoSink(m_cameraSink);
  connect(m_cameraSink, &QVideoSink::videoFrameChanged, this,
          [this](const QVideoFrame &frame) {
            if (frame.isValid() && !m_hasFrame) {
              m_hasFrame = true;
              emit changed();
            }
            if (m_previewSink)
              m_previewSink->setVideoFrame(frame);
            enqueueVideo(frame);
          });
  connect(m_camera, &QCamera::activeChanged, this, [this](bool active) {
    if (!active) m_hasFrame = false;
    emit changed();
  });
  connect(m_camera, &QCamera::errorOccurred, this,
          [this](QCamera::Error, const QString &message) { fail(message); });
  auto *devices = new QMediaDevices(this);
  connect(devices, &QMediaDevices::videoInputsChanged, this, [this] {
    const auto id = deviceId();
    refreshDevices();
    bool found = false;
    for (const auto &value : m_devices)
      found |= value.toMap().value("id").toString() == id;
    if (m_prepared && !found)
      fail(tr("The webcam was disconnected. Your voice takes are kept."));
  });
}

QString WebcamRecorder::deviceId() const {
  return m_camera ? QString::fromUtf8(m_camera->cameraDevice().id()) : QString();
}
bool WebcamRecorder::active() const { return m_camera && m_camera->isActive() && m_hasFrame; }
QString WebcamRecorder::formatLabel() const {
  return m_formatIndex >= 0 && m_formatIndex < m_formats.size()
             ? m_formats[m_formatIndex].toMap().value("label").toString()
             : QString();
}
void WebcamRecorder::setPreviewSink(QObject *sink) {
  m_previewSink = qobject_cast<QVideoSink *>(sink);
  if (m_previewSink)
    connect(m_previewSink, &QObject::destroyed, this,
            [this] { m_previewSink = nullptr; });
}
void WebcamRecorder::refreshDevices() {
  m_devices.clear();
  for (const auto &device : QMediaDevices::videoInputs())
    m_devices.append(QVariantMap{{"id", QString::fromUtf8(device.id())},
                                {"description", device.description()}});
  emit changed();
}
void WebcamRecorder::selectDevice(const QString &id) {
  if (m_prepared)
    return;
  ensureCamera();
  for (const auto &device : QMediaDevices::videoInputs()) {
    if (QString::fromUtf8(device.id()) != id)
      continue;
    const bool resume = m_camera->isActive();
    m_camera->stop();
    m_camera->setCameraDevice(device);
    m_formats.clear();
    const auto formats = device.videoFormats();
    int preferred = 0;
    for (int i = 0; i < formats.size(); ++i) {
      const auto &format = formats[i];
      m_formats.append(QVariantMap{{"label", tr("%1 × %2 · %3 fps")
          .arg(format.resolution().width()).arg(format.resolution().height())
          .arg(qMin(30.0f, format.maxFrameRate()), 0, 'f', 0)}});
      if (format.resolution() == QSize(1920, 1080) && format.maxFrameRate() >= 30)
        preferred = i;
    }
    m_formatIndex = -1;
    selectFormat(preferred);
    m_error.clear();
    if (resume)
      m_camera->start();
    emit changed();
    return;
  }
}
void WebcamRecorder::selectFormat(int index) {
  if (m_prepared || !m_camera)
    return;
  const auto formats = m_camera->cameraDevice().videoFormats();
  if (index < 0 || index >= formats.size())
    return;
  m_camera->setCameraFormat(formats[index]);
  m_formatIndex = index;
  emit changed();
}
void WebcamRecorder::setEnabled(bool enabled) {
  if (m_prepared)
    return;
  if (!enabled) {
    if (m_camera)
      m_camera->stop();
    if (m_previewSink)
      m_previewSink->setVideoFrame({});
    return;
  }
  ensureCamera();
  refreshDevices();
  if (m_devices.isEmpty()) {
    m_error = tr("No webcam found. Connect a camera or use Voice only.");
    emit changed();
    return;
  }
  bool selectedAvailable = false;
  for (const auto &value : m_devices)
    selectedAvailable |= value.toMap().value("id").toString() == deviceId();
  if (!selectedAvailable || m_formatIndex < 0)
    selectDevice(m_devices.first().toMap().value("id").toString());
  m_camera->start();
}

bool WebcamRecorder::prepare(const QString &path) {
  if (m_prepared)
    return false;
  delete m_capture;
  m_capture = nullptr;
  m_recorder = nullptr;
  m_audioInput = nullptr;
  m_videoInput = nullptr;
  m_path = path;
  m_error.clear();
  m_prepared = true;
  m_finishing = m_discard = m_audioEnded = m_videoEnded = false;
  m_lastVideoUs = -1;
  {
    QMutexLocker lock(&m_mutex);
    m_audio.clear();
    m_video.clear();
    m_pendingAudioUs = m_audioEndUs = 0;
    m_acceptAudio = true;
  }
  emit changed();
  return true;
}

void WebcamRecorder::startEncoding(const QAudioFormat &format) {
  if (!m_prepared || m_finishing)
    return;
  if (m_capture)
    delete m_capture;
  m_capture = new QMediaCaptureSession(this);
  m_audioInput = new QAudioBufferInput(format, m_capture);
  m_videoInput = new QVideoFrameInput(m_capture);
  m_recorder = new QMediaRecorder(m_capture);
  m_capture->setAudioBufferInput(m_audioInput);
  m_capture->setVideoFrameInput(m_videoInput);
  m_capture->setRecorder(m_recorder);
  QMediaFormat mediaFormat(QMediaFormat::MPEG4);
  mediaFormat.setVideoCodec(QMediaFormat::VideoCodec::H264);
  mediaFormat.setAudioCodec(QMediaFormat::AudioCodec::AAC);
  m_recorder->setMediaFormat(mediaFormat);
  m_recorder->setQuality(QMediaRecorder::HighQuality);
  m_recorder->setVideoFrameRate(30);
  if (m_camera && !m_camera->cameraFormat().isNull()) {
    const auto cameraFormat = m_camera->cameraFormat();
    m_recorder->setVideoResolution(cameraFormat.resolution());
    m_recorder->setVideoFrameRate(qBound(cameraFormat.minFrameRate(),
                                         30.0f, cameraFormat.maxFrameRate()));
  }
  m_recorder->setAutoStop(true);
  m_recorder->setOutputLocation(QUrl::fromLocalFile(m_path));
  connect(m_audioInput, &QAudioBufferInput::readyToSendAudioBuffer,
          this, &WebcamRecorder::drain);
  connect(m_videoInput, &QVideoFrameInput::readyToSendVideoFrame,
          this, &WebcamRecorder::drain);
  connect(m_recorder, &QMediaRecorder::errorOccurred, this,
          [this](QMediaRecorder::Error, const QString &error) { fail(error); });
  connect(m_recorder, &QMediaRecorder::recorderStateChanged, this,
          [this](QMediaRecorder::RecorderState state) {
            if (state == QMediaRecorder::StoppedState && m_prepared)
              complete();
            else
              drain();
          });
  m_recorder->record();
}

void WebcamRecorder::enqueueAudio(const QAudioBuffer &buffer) {
  QMutexLocker lock(&m_mutex);
  if (!m_acceptAudio || !buffer.isValid())
    return;
  if (m_pendingAudioUs + buffer.duration() > 2000000) {
    m_acceptAudio = false;
    QMetaObject::invokeMethod(this, [this] {
      fail(tr("Webcam encoding could not keep up. Your voice takes are kept."));
    }, Qt::QueuedConnection);
    return;
  }
  m_audio.enqueue(buffer);
  m_pendingAudioUs += buffer.duration();
  m_audioEndUs = buffer.startTime() + buffer.duration();
  if (!m_drainQueued) {
    m_drainQueued = true;
    QMetaObject::invokeMethod(this, &WebcamRecorder::drain, Qt::QueuedConnection);
  }
}

void WebcamRecorder::enqueueVideo(QVideoFrame frame) {
  if (!m_prepared || m_finishing || !frame.isValid())
    return;
  QMutexLocker lock(&m_mutex);
  // Audio samples define the clock. Camera device timestamps may have an
  // unrelated epoch, so replace them instead of muxing two independent clocks.
  if (m_audioEndUs <= 0)
    return;
  const qint64 time = m_audioEndUs;
  if (time <= m_lastVideoUs)
    return;
  frame.setStartTime(time);
  frame.setEndTime(-1);
  frame.setStreamFrameRate(m_recorder ? m_recorder->videoFrameRate() : 30);
  m_lastVideoUs = time;
  if (m_video.size() >= 3)
    m_video.dequeue();
  m_video.enqueue(frame);
  lock.unlock();
  drain();
}

void WebcamRecorder::drain() {
  {
    QMutexLocker lock(&m_mutex);
    m_drainQueued = false;
  }
  if (m_draining || !m_prepared || !m_recorder ||
      m_recorder->recorderState() != QMediaRecorder::RecordingState)
    return;
  m_draining = true;
  auto reset = qScopeGuard([this] { m_draining = false; });
  // Never hold the producer's mutex across an encoder call: errors and state
  // changes may be emitted synchronously, and the microphone must stay free.
  while (m_prepared) {
    QAudioBuffer buffer;
    {
      QMutexLocker lock(&m_mutex);
      if (m_audio.isEmpty()) break;
      buffer = m_audio.head();
    }
    if (!m_audioInput->sendAudioBuffer(buffer) || !m_prepared) break;
    QMutexLocker lock(&m_mutex);
    m_pendingAudioUs -= m_audio.head().duration();
    m_audio.dequeue();
  }
  while (m_prepared && !m_video.isEmpty() &&
         m_videoInput->sendVideoFrame(m_video.head()) && m_prepared)
    m_video.dequeue();
  if (m_finishing && m_prepared) {
    bool audioEmpty;
    {
      QMutexLocker lock(&m_mutex);
      audioEmpty = m_audio.isEmpty();
    }
    if (!m_audioEnded && audioEmpty)
      m_audioEnded = m_audioInput->sendAudioBuffer({});
    if (m_prepared && !m_videoEnded && m_video.isEmpty())
      m_videoEnded = m_videoInput->sendVideoFrame({});
  }
}
void WebcamRecorder::finish(bool discard) {
  if (!m_prepared)
    return;
  m_finishing = true;
  m_discard |= discard;
  {
    QMutexLocker lock(&m_mutex);
    m_acceptAudio = false;
  }
  if (!m_recorder || m_recorder->recorderState() == QMediaRecorder::StoppedState) {
    complete();
    return;
  }
  drain();
}
void WebcamRecorder::fail(const QString &message) {
  m_error = message;
  emit changed();
  emit failed(message);
  if (m_prepared) {
    m_discard = true;
    m_finishing = true;
    {
      QMutexLocker lock(&m_mutex);
      m_acceptAudio = false;
    }
    if (m_recorder && m_recorder->recorderState() != QMediaRecorder::StoppedState)
      m_recorder->stop();
    else
      complete();
  }
}
void WebcamRecorder::complete() {
  if (!m_prepared)
    return;
  const QString path = m_recorder && !m_recorder->actualLocation().isEmpty()
                           ? m_recorder->actualLocation().toLocalFile() : m_path;
  qint64 duration;
  {
    QMutexLocker lock(&m_mutex);
    m_acceptAudio = false;
    duration = m_audioEndUs / 1000;
    m_audio.clear();
    m_video.clear();
  }
  const bool discarded = m_discard || duration <= 0 || !QFile::exists(path);
  m_prepared = false;
  if (discarded)
    QFile::remove(path);
  emit changed();
  emit finished(path, duration, discarded);
}

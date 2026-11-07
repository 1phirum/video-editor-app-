#pragma once

#include "core/module_api.h"
#include <QAudioBuffer>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QVariantList>
#include <QVideoFrame>

class QCamera;
class QMediaCaptureSession;
class QMediaRecorder;
class QAudioBufferInput;
class QVideoFrameInput;
class QVideoSink;

// GUI-thread camera/encoder facade. PCM enters through a bounded, thread-safe
// queue from the existing microphone worker; no second microphone is opened.
class CUTPRO_PREVIEW_API WebcamRecorder final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
  Q_PROPERTY(QVariantList formats READ formats NOTIFY changed)
  Q_PROPERTY(QString deviceId READ deviceId NOTIFY changed)
  Q_PROPERTY(int formatIndex READ formatIndex NOTIFY changed)
  Q_PROPERTY(QString formatLabel READ formatLabel NOTIFY changed)
  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
public:
  explicit WebcamRecorder(QObject *parent = nullptr);
  ~WebcamRecorder() override;
  QVariantList devices() const { return m_devices; }
  QVariantList formats() const { return m_formats; }
  QString deviceId() const;
  int formatIndex() const { return m_formatIndex; }
  QString formatLabel() const;
  bool active() const;
  bool busy() const { return m_prepared; }
  QString error() const { return m_error; }
  Q_INVOKABLE void setPreviewSink(QObject *sink);
  Q_INVOKABLE void refreshDevices();
  Q_INVOKABLE void setEnabled(bool enabled);
  Q_INVOKABLE void selectDevice(const QString &id);
  Q_INVOKABLE void selectFormat(int index);
  bool prepare(const QString &path);
  void startEncoding(const QAudioFormat &format);
  // Safe on the microphone thread; does no device or encoder work there.
  void enqueueAudio(const QAudioBuffer &buffer);
  void enqueueVideo(QVideoFrame frame);
  void finish(bool discard = false);
signals:
  void changed();
  void finished(const QString &path, qint64 durationMs, bool discarded);
  void failed(const QString &error);
private:
  void ensureCamera();
  void drain();
  void fail(const QString &message);
  void complete();
  QCamera *m_camera = nullptr;
  QMediaCaptureSession *m_preview = nullptr;
  QVideoSink *m_cameraSink = nullptr;
  QVideoSink *m_previewSink = nullptr;
  QMediaCaptureSession *m_capture = nullptr;
  QMediaRecorder *m_recorder = nullptr;
  QAudioBufferInput *m_audioInput = nullptr;
  QVideoFrameInput *m_videoInput = nullptr;
  QVariantList m_devices, m_formats;
  int m_formatIndex = -1;
  QString m_path, m_error;
  bool m_prepared = false, m_finishing = false, m_discard = false;
  bool m_audioEnded = false, m_videoEnded = false;
  bool m_draining = false;
  bool m_hasFrame = false;
  qint64 m_lastVideoUs = -1;
  QMutex m_mutex;
  QQueue<QAudioBuffer> m_audio;
  QQueue<QVideoFrame> m_video;
  qint64 m_pendingAudioUs = 0, m_audioEndUs = 0;
  bool m_acceptAudio = false, m_drainQueued = false;
};

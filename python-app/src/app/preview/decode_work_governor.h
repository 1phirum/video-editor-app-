#pragma once

#include <QString>
#include <QVariantMap>
#include <QWaitCondition>
#include <QMutex>

#include <atomic>

// One admission gate in front of every on-demand preview decode.
//
// The preview subsystem grew three independent workers: the scrub still service
// (one thread), the filmstrip/tile provider (two threads) and the waveform
// window provider (two threads), each holding its own decoder sessions, on top
// of the playback decoder's own thread and its four to eight decoder threads.
// Nothing coordinated them. On a large, expensive source they all wake at once -
// a drop, a scroll and a zoom all produce dozens of requests - and together they
// ask for more CPU and more disk than the machine has. The GUI thread is then
// just another contender for a core it needs to keep the window painting, so
// Windows marks the app "Not Responding" even though every worker is making
// progress.
//
// Two rules, both about giving the interface priority over pictures it is not
// showing yet:
//
//  * a bounded number of concurrent decodes, so background work cannot fill
//    every core;
//  * while the user is dragging, trimming or scrubbing, filmstrip and waveform
//    work is refused outright. Those are decorations for a state the user is
//    still changing; the frame under the playhead is not, so interactive
//    requests keep their slot.
//
// The interaction flag carries its own deadline. A QML path that begins an
// interaction and never ends it - a native drag loop that swallows the release,
// a window that loses focus mid-drag - would otherwise leave the timeline
// permanently undecorated, so the flag lapses on its own and any later end() is
// harmless.
class DecodeWorkGovernor final {
public:
  enum class Class {
    // The picture under the playhead: what the user is waiting for.
    Interactive,
    // Timeline thumbnails.
    Filmstrip,
    // Waveform windows.
    Waveform,
  };

  // Holds a decode slot for its lifetime. A refused admission is not an error:
  // callers report it as a cancelled request, which the providers already draw
  // as a placeholder.
  class Admission final {
  public:
    Admission() = default;
    ~Admission();
    Admission(Admission &&other) noexcept;
    Admission &operator=(Admission &&other) noexcept;
    Admission(const Admission &) = delete;
    Admission &operator=(const Admission &) = delete;

    bool granted() const { return m_owner != nullptr; }

  private:
    friend class DecodeWorkGovernor;
    explicit Admission(DecodeWorkGovernor *owner) : m_owner(owner) {}
    DecodeWorkGovernor *m_owner = nullptr;
  };

  static DecodeWorkGovernor &instance();

  // Waits up to waitMs for a slot. Returns an ungranted admission when the
  // wait expires, or immediately when background work is being held back.
  //
  // Called on the GUI thread the wait is forced to zero, whatever the caller
  // asked for. That thread cannot afford to park on a decode slot, and a caller
  // reached from a QML handler has no way to know which thread it is on.
  Admission admit(Class kind, int waitMs);

  // Try-acquire. Never blocks, never touches the condition variable. For any
  // path where waiting is worse than going without the frame.
  Admission tryAdmit(Class kind);

  // Ref-counted, so a drag inside a scrub does not end the hold early.
  void beginInteraction();
  void endInteraction();
  // Extends the deadline without changing the count; called from the pointer
  // move handlers that already run per drag step.
  void touchInteraction();
  bool interactionActive() const;

  // `slots` is a Qt keyword macro, so the parameter cannot be named after it.
  void setConcurrency(int decodeSlots);
  int concurrency() const;
  QVariantMap statistics() const;

  // Long enough that the gap between two drag steps never lifts the hold,
  // short enough that a lost end() is invisible.
  static constexpr qint64 kInteractionLapseMs = 1200;

private:
  DecodeWorkGovernor();

  void release();
  bool holdingBackLocked() const;

  mutable QMutex m_mutex;
  QWaitCondition m_slotFreed;
  int m_slots = 2;
  int m_active = 0;
  int m_interactionCount = 0;
  qint64 m_interactionDeadlineMs = 0;

  std::atomic<quint64> m_granted{0};
  std::atomic<quint64> m_refusedBusy{0};
  std::atomic<quint64> m_refusedInteraction{0};
  std::atomic<quint64> m_refusedGuiThread{0};
  std::atomic<quint64> m_interactions{0};
};

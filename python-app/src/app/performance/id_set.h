#pragma once

// Membership tests for the id lists a delegate asks about once per frame.
//
// TimelinePanel.qml answers "is this clip selected?" like this:
//
//     function isClipSelected(id) { return selectedClipIds.indexOf(id) >= 0 }
//
// and a delegate calls it from a binding on its border. indexOf is a linear
// scan with a string compare per element, which costs nothing for one selected
// clip and is not free for Select All over a transcript track: 19 831 ids, one
// scan per delegate, up to 420 delegates on screen, re-run whenever the list
// changes. The work is quadratic in the size of the selection and it buys a
// border width.
//
// A hash set answers the same question in one lookup. The revision counter is
// the other half of the fix: a QML binding cannot take a dependency on a C++
// container, so the owner exposes an int that changes on every mutation and the
// binding depends on that - the same pattern MonitorPanel's clipRevision
// property already uses to make a Q_INVOKABLE result re-evaluate.
//
// Insertion order is deliberately not kept. Membership is the only thing the
// hot path asks for, and a caller that also needs "the last one clicked"
// already keeps its own ordered list and can mirror writes into one of these.
//
// Header-only and not a QObject, so it can sit as a member of whatever owns the
// selection today without changing that class's QML-visible interface. Not
// thread safe.

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QtGlobal>

namespace cutpro::perf {

class IdSet {
public:
  IdSet() = default;

  // Replaces the contents. Returns true when the set actually changed, which is
  // what the caller should gate its notify signal on: a drag that re-asserts
  // the same selection every frame should not make every delegate re-evaluate.
  bool setIds(const QSet<QString> &ids) {
    if (ids == m_ids)
      return false;
    m_ids = ids;
    bump();
    return true;
  }

  bool setIds(const QStringList &ids) {
    QSet<QString> next;
    next.reserve(ids.size());
    for (const QString &id : ids) {
      if (!id.isEmpty())
        next.insert(id);
    }
    return setIds(next);
  }

  // For the QVariantList that crosses the QML boundary. Empty and non-string
  // entries are dropped rather than stored as an id nothing can ever match.
  bool setIds(const QVariantList &ids) {
    QSet<QString> next;
    next.reserve(ids.size());
    for (const QVariant &value : ids) {
      const QString id = value.toString();
      if (!id.isEmpty())
        next.insert(id);
    }
    return setIds(next);
  }

  bool insert(const QString &id) {
    if (id.isEmpty() || m_ids.contains(id))
      return false;
    m_ids.insert(id);
    bump();
    return true;
  }

  bool remove(const QString &id) {
    if (!m_ids.remove(id))
      return false;
    bump();
    return true;
  }

  // Returns membership *after* the toggle, which is what a ctrl-click handler
  // wants to know.
  bool toggle(const QString &id) {
    if (id.isEmpty())
      return false;
    if (m_ids.remove(id)) {
      bump();
      return false;
    }
    m_ids.insert(id);
    bump();
    return true;
  }

  bool clear() {
    if (m_ids.isEmpty())
      return false;
    m_ids.clear();
    bump();
    return true;
  }

  bool contains(const QString &id) const { return m_ids.contains(id); }

  // How many of `ids` are in the set, without building an intermediate list.
  // For a "3 of 5 selected" state on a group header.
  int countIn(const QStringList &ids) const {
    int hits = 0;
    for (const QString &id : ids) {
      if (m_ids.contains(id))
        ++hits;
    }
    return hits;
  }

  bool containsAny(const QStringList &ids) const {
    for (const QString &id : ids) {
      if (m_ids.contains(id))
        return true;
    }
    return false;
  }

  bool containsAll(const QStringList &ids) const {
    for (const QString &id : ids) {
      if (!m_ids.contains(id))
        return false;
    }
    return !ids.isEmpty();
  }

  bool isEmpty() const { return m_ids.isEmpty(); }
  int size() const { return m_ids.size(); }
  const QSet<QString> &ids() const { return m_ids; }
  QStringList toList() const {
    return QStringList(m_ids.cbegin(), m_ids.cend());
  }
  QVariantList toVariantList() const {
    QVariantList out;
    out.reserve(m_ids.size());
    for (const QString &id : m_ids)
      out.append(id);
    return out;
  }

  // Increments on every mutation, so a binding on it re-evaluates exactly when
  // the answer could have changed and not otherwise.
  quint32 revision() const { return m_revision; }

private:
  void bump() { ++m_revision; }

  QSet<QString> m_ids;
  quint32 m_revision = 1;
};

} // namespace cutpro::perf

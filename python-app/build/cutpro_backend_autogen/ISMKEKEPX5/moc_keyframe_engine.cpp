/****************************************************************************
** Meta object code from reading C++ file 'keyframe_engine.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/app/keyframe_engine.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'keyframe_engine.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN14KeyframeEngineE_t {};
} // unnamed namespace

template <> constexpr inline auto KeyframeEngine::qt_create_metaobjectdata<qt_meta_tag_ZN14KeyframeEngineE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "KeyframeEngine",
        "keyframesChanged",
        "",
        "clipId",
        "addKeyframe",
        "property",
        "timeMs",
        "QVariant",
        "value",
        "removeKeyframe",
        "toggleKeyframing",
        "interpolatedValue",
        "keyframesFor",
        "QVariantList",
        "isKeyframed",
        "previousTime",
        "nextTime"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'keyframesChanged'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Method 'addKeyframe'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, qint64, const QVariant &)>(4, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 5 }, { QMetaType::LongLong, 6 }, { 0x80000000 | 7, 8 },
        }}),
        // Method 'removeKeyframe'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, qint64)>(9, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 5 }, { QMetaType::LongLong, 6 },
        }}),
        // Method 'toggleKeyframing'
        QtMocHelpers::MethodData<bool(const QString &, const QString &, qint64, const QVariant &)>(10, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 5 }, { QMetaType::LongLong, 6 }, { 0x80000000 | 7, 8 },
        }}),
        // Method 'interpolatedValue'
        QtMocHelpers::MethodData<QVariant(const QString &, const QString &, qint64) const>(11, 2, QMC::AccessPublic, 0x80000000 | 7, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 5 }, { QMetaType::LongLong, 6 },
        }}),
        // Method 'keyframesFor'
        QtMocHelpers::MethodData<QVariantList(const QString &, const QString &) const>(12, 2, QMC::AccessPublic, 0x80000000 | 13, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 5 },
        }}),
        // Method 'isKeyframed'
        QtMocHelpers::MethodData<bool(const QString &, const QString &) const>(14, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 5 },
        }}),
        // Method 'previousTime'
        QtMocHelpers::MethodData<qint64(const QString &, const QString &, qint64) const>(15, 2, QMC::AccessPublic, QMetaType::LongLong, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 5 }, { QMetaType::LongLong, 6 },
        }}),
        // Method 'nextTime'
        QtMocHelpers::MethodData<qint64(const QString &, const QString &, qint64) const>(16, 2, QMC::AccessPublic, QMetaType::LongLong, {{
            { QMetaType::QString, 3 }, { QMetaType::QString, 5 }, { QMetaType::LongLong, 6 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<KeyframeEngine, qt_meta_tag_ZN14KeyframeEngineE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject KeyframeEngine::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14KeyframeEngineE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14KeyframeEngineE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14KeyframeEngineE_t>.metaTypes,
    nullptr
} };

void KeyframeEngine::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<KeyframeEngine *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->keyframesChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: { bool _r = _t->addKeyframe((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 2: { bool _r = _t->removeKeyframe((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->toggleKeyframing((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QVariant>>(_a[4])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { QVariant _r = _t->interpolatedValue((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])));
            if (_a[0]) *reinterpret_cast<QVariant*>(_a[0]) = std::move(_r); }  break;
        case 5: { QVariantList _r = _t->keyframesFor((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 6: { bool _r = _t->isKeyframed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 7: { qint64 _r = _t->previousTime((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        case 8: { qint64 _r = _t->nextTime((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])));
            if (_a[0]) *reinterpret_cast<qint64*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (KeyframeEngine::*)(const QString & )>(_a, &KeyframeEngine::keyframesChanged, 0))
            return;
    }
}

const QMetaObject *KeyframeEngine::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *KeyframeEngine::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14KeyframeEngineE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int KeyframeEngine::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}

// SIGNAL 0
void KeyframeEngine::keyframesChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
QT_WARNING_POP

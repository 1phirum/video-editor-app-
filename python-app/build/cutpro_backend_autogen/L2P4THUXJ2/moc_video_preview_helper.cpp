/****************************************************************************
** Meta object code from reading C++ file 'video_preview_helper.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/app/preview/video_preview_helper.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'video_preview_helper.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN18VideoPreviewHelperE_t {};
} // unnamed namespace

template <> constexpr inline auto VideoPreviewHelper::qt_create_metaobjectdata<qt_meta_tag_ZN18VideoPreviewHelperE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "VideoPreviewHelper",
        "activeStateChanged",
        "",
        "activeClip",
        "QVariant",
        "activeMedia",
        "activeAudioClip",
        "activeAudioClips",
        "QVariantList",
        "activeSubtitle",
        "activeClipId",
        "uiTickInterval"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'activeStateChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'activeClip'
        QtMocHelpers::PropertyData<QVariant>(3, 0x80000000 | 4, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 0),
        // property 'activeMedia'
        QtMocHelpers::PropertyData<QVariant>(5, 0x80000000 | 4, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 0),
        // property 'activeAudioClip'
        QtMocHelpers::PropertyData<QVariant>(6, 0x80000000 | 4, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 0),
        // property 'activeAudioClips'
        QtMocHelpers::PropertyData<QVariantList>(7, 0x80000000 | 8, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 0),
        // property 'activeSubtitle'
        QtMocHelpers::PropertyData<QVariant>(9, 0x80000000 | 4, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 0),
        // property 'activeClipId'
        QtMocHelpers::PropertyData<QString>(10, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'uiTickInterval'
        QtMocHelpers::PropertyData<int>(11, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Constant),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<VideoPreviewHelper, qt_meta_tag_ZN18VideoPreviewHelperE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject VideoPreviewHelper::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18VideoPreviewHelperE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18VideoPreviewHelperE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18VideoPreviewHelperE_t>.metaTypes,
    nullptr
} };

void VideoPreviewHelper::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<VideoPreviewHelper *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->activeStateChanged(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (VideoPreviewHelper::*)()>(_a, &VideoPreviewHelper::activeStateChanged, 0))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QVariant*>(_v) = _t->activeClip(); break;
        case 1: *reinterpret_cast<QVariant*>(_v) = _t->activeMedia(); break;
        case 2: *reinterpret_cast<QVariant*>(_v) = _t->activeAudioClip(); break;
        case 3: *reinterpret_cast<QVariantList*>(_v) = _t->activeAudioClips(); break;
        case 4: *reinterpret_cast<QVariant*>(_v) = _t->activeSubtitle(); break;
        case 5: *reinterpret_cast<QString*>(_v) = _t->activeClipId(); break;
        case 6: *reinterpret_cast<int*>(_v) = _t->uiTickInterval(); break;
        default: break;
        }
    }
}

const QMetaObject *VideoPreviewHelper::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *VideoPreviewHelper::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18VideoPreviewHelperE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int VideoPreviewHelper::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void VideoPreviewHelper::activeStateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}
QT_WARNING_POP

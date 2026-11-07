import sys
import os
from PySide6.QtCore import QObject, Property, Slot, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine, qmlRegisterSingletonInstance
from PySide6.QtQuickControls2 import QQuickStyle

class MockBackend(QObject):
    def __init__(self):
        super().__init__()
        self._videoTrackCount = 3
        self._audioTrackCount = 3
        self._hasSubtitleClips = False
        self._clips = []
        self._playheadMs = 0
        self._mutedTracks = []

    @Property(int)
    def videoTrackCount(self): return self._videoTrackCount

    @Property(int)
    def audioTrackCount(self): return self._audioTrackCount

    @Property(bool)
    def hasSubtitleClips(self): return self._hasSubtitleClips

    @Property(list)
    def clips(self): return self._clips

    @Property(int)
    def playheadMs(self): return self._playheadMs

    @Property(list)
    def mutedTracks(self): return self._mutedTracks

    @Slot(str, result=bool)
    def trackVisible(self, trackId): return True

    @Slot(str, result=bool)
    def trackLocked(self, trackId): return False

    @Slot(str, result='QVariantMap')
    def trackState(self, trackId): return {"solo": False, "syncLocked": True, "targeted": True}

    @Slot(str, str, bool)
    def setTrackState(self, trackId, stateKey, value): pass

    @Slot(str, bool)
    def setTrackMuted(self, trackId, muted): pass


if __name__ == "__main__":
    app = QGuiApplication(sys.argv)
    
    QQuickStyle.setStyle("Basic")
    
    backend = MockBackend()
    
    engine = QQmlApplicationEngine()
    
    # Register the mock backend as a singleton
    qmlRegisterSingletonInstance(MockBackend, "CutPro", 1, 0, "Backend", backend)
    
    # Load the QML from the local file system (so it updates instantly without rebuilding)
    qml_file = os.path.join(os.path.dirname(__file__), "qml", "main.qml")
    engine.load(QUrl.fromLocalFile(qml_file))
    
    if not engine.rootObjects():
        sys.exit(-1)
        
    sys.exit(app.exec())

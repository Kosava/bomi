import QtQuick 2.0
import QtQuick.Layouts 1.0
import bomi 1.0

Item {
    id: wrapper
    readonly property int fontSize: parent.height*0.022;
    width: parent.width-fontSize*2;
    height: parent.height-fontSize*2;
    anchors.centerIn: parent
    property string fontFamily: Util.monospace
    property alias show: wrapper.visible
    readonly property Engine engine: App.engine
    readonly property var audio: engine.audio
    readonly property var video: engine.video
    readonly property var sub: engine.subtitle

    onVisibleChanged: if (visible) bringIn.start()
    NumberAnimation {
        id: bringIn; target: box; properties: "scale"; running: false
        from: 0.0;     to: 1.0; easing {type: Easing.OutBack; overshoot: 1.1}
    }
    visible: false

    property real __cpu: 0.0
    property real __mem: 0.0

    Timer {
        running: parent.visible
        interval: 1000
        repeat: true
        onTriggered: {
            __cpu = Util.cpu
            __mem = Util.memory
        }
    }
    ColumnLayout {
        id: box; spacing: 0
        readonly property alias fontSize: wrapper.fontSize
        PlayInfoText { text: engine.media.name }
        PlayInfoText {
            readonly property int time: engine.time/1000
            readonly property int end: engine.end/1000
            readonly property int pos10: engine.rate*1000
            text: qsTr("State: %2/%3(%4%) ×%5 [%1]").arg(engine.stateText)
                .arg(Util.secToString(time)).arg(Util.secToString(end))
                .arg(time > 0 && end > 0 ? (pos10/10.0).toFixed(1) : 0)
                .arg(engine.speed.toFixed(2))
        }
        PlayInfoText {
            readonly property int sync: engine.avSync
            text: qsTr("Audio/Video Sync: %1%2ms")
                .arg(sync < 0 ? "" : sync > 0 ? "+" : "±").arg(sync);
        }

        PlayInfoText { }

        PlayInfoText {
            property alias cpu: wrapper.__cpu
            text: qsTr("CPU Usage: %1%(avg. %2%/core)")
                .arg(cpu.toFixed(0)).arg((cpu/Util.cores).toFixed(1));
        }
        PlayInfoText {
            property alias mem: wrapper.__mem
            text: qsTr("RAM Usage: %3MiB(%4% of %5GiB)")
                .arg(mem.toFixed(1)).arg((mem/Util.totalMemory*100.0).toFixed(1))
                .arg((Util.totalMemory/1024.0).toFixed(2));
        }
        PlayInfoText {
            readonly property int used: engine.cacheUsed
            readonly property int size: engine.cacheSize
            readonly property real percent: 100.0*used/size
            text: qsTr("Cache: %1")
                .arg(!size ? qsTr("Unavailable")
                           : qsTr("%1KiB(%3% of %2KiB)").arg(used).arg(size)
                                .arg(percent.toFixed(1)))
        }

        PlayInfoText { }

        PlayInfoTrack { id: ti; name: qsTr("Video Track"); info: video }
        PlayInfoVideoOutput { format: video.input; name: qsTr("Input   ") }
        PlayInfoVideoOutput { format: video.output; name: qsTr("Output  ") }
        PlayInfoVideoOutput { format: video.renderer; name: qsTr("Renderer") }

        function activationText(s) {
            switch (s) {
            case Engine.Unavailable: return qsTr("Unavailable")
            case Engine.Deactivated: return qsTr("Deactivated")
            case Engine.Activated:   return qsTr("Activated")
            default:                 return ""
            }
        }

        PlayInfoText {
            text: qsTr("Dropped Frames: %1 (%2fps)").arg(video.droppedFrames).arg(video.droppedFps.toFixed(3));
        }
        PlayInfoText {
            text: qsTr("Delayed Frames: %1 (%2ms)").arg(video.delayedFrames).arg(video.delayedTime);
        }

        PlayInfoText {
            readonly property var hw: video.hwacc
            function driverText(d) { return (d === "" || d === "no") ? "--" : d }
            text: qsTr("Hardware Acceleration: %1[%2]")
                .arg(box.activationText(hw.state)).arg(driverText(hw.driver))
        }

        PlayInfoText {
            readonly property var hw: video.hwacc
            function driverText(d) { return (d === "" || d === "no") ? "--" : d }
            text: qsTr("Deinterlacer: %3")
                .arg(box.activationText(video.deinterlacer))
        }

        PlayInfoText { }

        PlayInfoTrack { name: qsTr("Audio Track"); info: audio }
        PlayInfoAudioOutput { format: audio.input; name: qsTr("Input   ") }
        PlayInfoAudioOutput { format: audio.output; name: qsTr("Output  ") }
        PlayInfoAudioOutput { format: audio.renderer;  name: qsTr("Renderer") }
        PlayInfoText {
            readonly property real gain: audio.normalizer * 100
            text: qsTr("Normalizer: %1[%2%]")
                .arg(gain < 0 ? qsTr("Deactivated") : qsTr("Activated"))
                .arg(gain < 0 ? "--" : gain.toFixed(1))
        }
        PlayInfoText {
            text: qsTr("Driver: %1[%2]")
                .arg(audio.driver.length > 0 ? audio.driver : "--")
                .arg(audio.device)
        }

        PlayInfoText { }
        PlayInfoSubtitleList { list: sub.files; name: qsTr("Subtitle File") }
        PlayInfoSubtitleList { list: sub.tracks; name: qsTr("Subtitle Track") }
    }
}

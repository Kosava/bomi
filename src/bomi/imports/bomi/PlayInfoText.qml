import QtQuick 2.0
import bomi 1.0

Text {
    id: item
    font { pixelSize: parent.fontSize; family: Util.monospace }
    color: "yellow"; styleColor: "black"
    style: Text.Outline;
    property alias timer: t.running
    property alias interval: t.interval
    signal tick

    function formatNumber(v, n) {
        return v > 0 ? ("" + (n === undefined ? v : v.toFixed(n))) : "--";
    }
    function formatSize(s) {
        return formatNumber(s.width) + "x" + formatNumber(s.height);
    }
    function formatText(s) { return s.length > 0 ? s : "--" }

    Timer { id: t; interval: 1000; repeat: true; onTriggered: item.tick() }
}

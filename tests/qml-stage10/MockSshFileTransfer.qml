import QtQml

QtObject {
    property bool busy: false
    property real transferredBytes: 0
    property var calls: []
    signal finished(string action, var result)
    signal hostKeyRequired(string endpoint, string fingerprint)
    function homePath() { return "/tmp"; }
    function localPath(url) { return String(url).replace(/^file:\/\//, ""); }
    function browseLocal(path) { run("local", {}, "", path, "", ""); }
    function run(action, options, remote, local, name, newName) {
        calls = calls.concat([{action: action, remote: remote, local: local, name: name, newName: newName}]);
        busy = true;
    }
    function complete(action, result) { busy = false; finished(action, result); }
    function cancel() {}
}

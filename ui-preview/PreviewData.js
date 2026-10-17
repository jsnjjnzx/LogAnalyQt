.pragma library

// UI fixtures only: these bytes are not validated protocol frames.
var sampleLines = [
    "10:24:16.182  [INFO]  连接已建立",
    "10:24:16.204  [TX]    68 17 00 43 05 01 00 00 01 03 00 00 16",
    "10:24:16.236  [RX]    68 1E 00 83 05 01 20 00 02 00 10 27 A1 16",
    "10:24:17.002  [INFO]  接收报文完成",
    "10:24:17.418  [TX]    68 11 00 C1 05 01 00 FF 00 16",
    "10:24:17.562  [RX]    68 0F 00 01 C1 05 00 00 46 16",
    "10:24:18.003  [INFO]  心跳检测",
    "10:24:18.221  [TX]    68 10 00 43 05 01 00 00 00 16",
    "10:24:18.389  [RX]    68 14 00 83 05 01 20 01 00 64 1F 16",
    "10:24:19.005  [INFO]  设备状态：正常",
    "10:24:20.112  [TX]    68 16 00 53 05 01 00 00 01 00 02 16",
    "10:24:20.347  [RX]    68 1A 00 85 05 01 20 00 02 01 12 33 16",
    "10:24:21.091  [INFO]  周期读取完成",
    "10:24:22.563  [TX]    68 11 00 C1 05 01 00 00 FF 16",
    "10:24:22.781  [RX]    68 0F 00 05 C1 05 01 00 00 16",
    "10:24:23.004  [INFO]  断开连接请求",
    "10:24:23.119  [TX]    68 04 00 63 16",
    "10:24:23.221  [RX]    68 04 00 63 16",
    "10:24:23.222  [INFO]  连接已断开"
];
var fields = [
    {name:"起始符", value:"68", description:"帧起始", offset:0, length:1, depth:0},
    {name:"长度域", value:"1E 00", description:"帧长度", offset:1, length:2, depth:0},
    {name:"控制域", value:"83", description:"服务器响应", offset:3, length:1, depth:0, group:"control"},
    {name:"方向", value:"1", description:"服务器 → 客户端", offset:3, length:1, depth:1, parent:"control"},
    {name:"地址域", value:"05 01", description:"服务器地址", offset:4, length:2, depth:0},
    {name:"APDU", value:"20 00 02 00", description:"数据对象（示例）", offset:6, length:4, depth:0, group:"apdu"},
    {name:"OAD", value:"20 00 02 00", description:"数据对象", offset:6, length:4, depth:1, parent:"apdu"},
    {name:"帧校验", value:"10 27 A1", description:"示意字段 · 未校验", offset:10, length:3, depth:0},
    {name:"结束符", value:"16", description:"帧结束", offset:13, length:1, depth:0}
];

function entries(lines, pattern) {
    var expression = pattern ? new RegExp(pattern, "i") : null;
    var result = [];
    for (var i = 0; i < lines.length; ++i) {
        if (!expression || expression.test(lines[i]))
            result.push({line:i, text:lines[i]});
    }
    return result;
}
function hex(line) {
    var match = line.match(/(?:[0-9a-fA-F]{2}[ \t]+){3,}[0-9a-fA-F]{2}\b/);
    return match ? match[0].trim().toUpperCase().split(/\s+/) : [];
}
function resultFields(line, sample) {
    var bytes = hex(line);
    if (!bytes.length) return [];
    if (sample && line === sampleLines[2]) return fields;
    // shortcut: unrecognized input shows raw bytes; replace with LogBackend during engine integration.
    return [{name:"原始报文", value:bytes.join(" "), description:"尚未接入解析引擎", offset:0, length:bytes.length, depth:0}];
}
function visibleFields(rows, collapsed) {
    return rows.filter(function(row) { return !row.parent || !collapsed[row.parent]; });
}
function find(entries, query, fromLine, backwards) {
    if (!query || !entries.length) return -1;
    var matches = entries.filter(function(entry) {
        return entry.text.toLowerCase().indexOf(query.toLowerCase()) >= 0;
    });
    if (!matches.length) return -1;
    if (backwards) {
        for (var i=matches.length-1; i>=0; --i)
            if (matches[i].line < fromLine) return matches[i].line;
        return matches[matches.length-1].line;
    }
    for (var j=0; j<matches.length; ++j)
        if (matches[j].line > fromLine) return matches[j].line;
    return matches[0].line;
}
function escapeHtml(value) {
    return value.replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;");
}
function richLine(line, query, byteOffset, byteLength) {
    var spans = [];
    if (query) {
        var at = 0;
        while ((at = line.toLowerCase().indexOf(query.toLowerCase(), at)) !== -1) {
            spans.push({start:at,end:at+query.length,color:"#ffd78c"});
            at += query.length;
        }
    }
    var bytes = hex(line);
    if (byteOffset >= 0 && bytes.length) {
        var first = line.search(/(?:[0-9a-fA-F]{2}[ \t]+){3,}[0-9a-fA-F]{2}\b/);
        var re = /[0-9a-fA-F]{2}/g, match, n=0;
        re.lastIndex = first;
        while ((match = re.exec(line)) !== null) {
            if (n >= byteOffset && n < byteOffset + byteLength)
                spans.push({start:match.index,end:match.index+2,color:"#83bdff"});
            ++n;
        }
    }
    var result="";
    for (var i=0; i<line.length; ++i) {
        var color="";
        for (var s=0; s<spans.length; ++s)
            if (i>=spans[s].start && i<spans[s].end) color=spans[s].color;
        var letter=line[i] === " " ? "&nbsp;" : escapeHtml(line[i]);
        result += color ? '<font color="'+color+'">'+letter+'</font>' : letter;
    }
    return result.replace(/\[INFO\]/g,'<font color="#67d2ba">[INFO]</font>')
                 .replace(/\[TX\]/g,'<font color="#51b0ff">[TX]</font>')
                 .replace(/\[RX\]/g,'<font color="#e7b56a">[RX]</font>');
}

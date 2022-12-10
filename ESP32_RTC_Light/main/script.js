// Get stored Data from server
// Time Settings
var onHr = "00";
var onMn = "00";
var offHr = "00";
var offMn = "00";
// Time Server Settings
var timeServer = "pool.ntp.org";
// AP Settings
var ssid = "ap";
var passwd = "";
// OTA
var otaServer = "";

window.addEventListener('load', function () {
    loadData();
})

async function loadData() {
    const response = await fetch('/apiget');
    const stuff = await response.json();
    console.log(stuff);
    // Time Settings
    onHr = stuff.onHr;
    onMn = stuff.onMn;
    offHr = stuff.offHr;
    offMn = stuff.offMn;
    // Time Server Settings
    timeServer = stuff.timeServer;
    // AP Settings
    ssid = stuff.ssid;
    passwd = stuff.passwd;
    // OTA
    otaServer = stuff.otaServer;

    // Set stuff to stored data
    // Time Settings
    document.getElementById('timeOn').value = onHr + ":" + onMn;
    document.getElementById('timeOff').value = offHr + ":" + offMn;
    // Time Server Settings
    document.getElementById('timeServer').value = timeServer;
    // AP Settings
    document.getElementById('ssid').value = ssid;
    // document.getElementById('passwd').value = passwd;
    // OTA
    document.getElementById('otaServer').value = otaServer;
}

function stb() {
    console.debug("Yeet");
    fetch("/sleep");
    alert("Device is going into deep sleep. You may close the tab.");
    window.close();
}

function save() {
    // Time Settings
    onHr = document.getElementById('timeOn').value.substring(0, 2);
    onMn = document.getElementById('timeOn').value.substring(3, 5);
    offHr = document.getElementById('timeOff').value.substring(0, 2);
    offMn = document.getElementById('timeOff').value.substring(3, 5);
    // Time Server Settings
    timeServer = document.getElementById('timeServer').value;
    // AP Settings
    ssid = document.getElementById('ssid').value;
    // If value changed
    if (document.getElementById('passwdChanged').checked) {
        passwd = document.getElementById('passwd').value;
    }
    // OTA
    otaServer = document.getElementById('otaServer').value;

    const params = {
        // Time Settings
        onHr: onHr,
        onMn: onMn,
        offHr: offHr,
        offMn: offMn,
        // Time Server Settings
        timeServer: timeServer,
        // AP Settings
        ssid: ssid,
        passwd: passwd,
        // OTA
        otaServer: otaServer,
    }
    const options = {
        method: 'POST',
        body: JSON.stringify(params)
    }
    console.debug("Save pressed")
    fetch('/api', options)
        .then(response => console.log(response.json()))
    alert("Saving changes, resetting device! If the AP still comes up, check that a connection to your WIFI is possible otherwise you may close the tab.");
    window.close();
}

function ota() {
    alert("OTA Update started");
    fetch("/ota")
}
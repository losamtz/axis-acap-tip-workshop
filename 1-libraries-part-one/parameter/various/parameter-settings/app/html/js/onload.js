window.onload = async function () {
    const response = await fetch('/axis-cgi/param.cgi?action=list&group=Parametersettings.*');
    if (!response.ok) {
        alert('Network response was not ok');
        return;
    }
    const data = await response.text();

    // const secretResponse = await fetch('/local/parametersettings/param-acap.cgi');
    // if (!secretResponse.ok) {
    //     alert('Network response was not ok');
    //     return;
    // }
    // console.log('Secret response status:', secretResponse.status);
    // const secret = await secretResponse.text();
    // console.log('Secret:', secret);
    
    const lines = data.split('\n');
    const result = {};

    lines.forEach(line => {
        const match = line.match(/root\.Parametersettings\.(\w+)=([\s\S]*)/);
        if (match) {
            const key = match[1];
            const value = match[2];
            result[key] = value;
        }
    });
    document.getElementById('intervalSecs').value = result['IntervalSecs'];
    document.getElementById('multicastAddress').value = result['MulticastAddress'];
    document.getElementById('multicastPort').value = result['MulticastPort'];
    document.getElementById('onvifUsername').value = result['OnvifUsername'];
    document.getElementById('password').value = result['Password'];
    //document.getElementById('password').value = secret ? secret : "";
    

};


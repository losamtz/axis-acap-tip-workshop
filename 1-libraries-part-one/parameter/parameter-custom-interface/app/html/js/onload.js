window.onload = async function () {
    const response = await fetch('/axis-cgi/param.cgi?action=list&group=parameter_custom_interface.*');
    if (!response.ok) {
        alert('Network response was not ok');
        return;
    }
    const data = await response.text();
    
    
    const lines = data.split('\n');
    const result = {};
    
    lines.forEach(line => {
        const match = line.match(/root\.Parameter_custom_interface\.(\w+)=([\s\S]*)/);
        
        if (match) {
            
            const key = match[1];
            const value = match[2];
            result[key] = value;
            
        }
    });
    
    document.getElementById('multicastAddress').value = result['MulticastAddress'];
    document.getElementById('multicastPort').value = result['MulticastPort'];
};


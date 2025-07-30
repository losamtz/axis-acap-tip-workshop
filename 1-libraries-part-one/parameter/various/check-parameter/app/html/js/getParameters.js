 // Listen for HTMX after content swap
document.body.addEventListener("htmx:beforeSwap", function (event) {
    console.log("🔍 htmx:beforeSwap triggered", event);

    const response = event.detail.xhr.responseText;
    console.log("📦 Raw response text:", response);

    
    if (!response) {
         console.warn("⚠️ response not found in response text.");
         return;
    }
    
    const rawText = response;
    const lines = rawText.split("\n").filter(line => line.includes("="));
    const keys = lines.map(line => line.split("=")[0]);
    console.log("📋 Extracted keys:", keys);

    const ul = document.createElement("ul");
    keys.forEach(key => {
        const li = document.createElement("li");
        li.textContent = key;
        ul.appendChild(li);
    });

    const content = document.getElementById("content");
    content.innerHTML = ""; // Clear old content
    content.appendChild(ul);

    event.preventDefault(); // Cancel the default swap
});



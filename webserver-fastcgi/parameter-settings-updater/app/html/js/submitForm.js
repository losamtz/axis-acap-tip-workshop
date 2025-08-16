document.getElementById('settingsForm').addEventListener('submit', async function (event) {
    event.preventDefault(); // Prevent default form submission

    const formData = new FormData(event.target);
    const baseUrl = '/axis-cgi/param.cgi?action=update&';
    const root = 'root.Parametersettings.';

    let fullUrl = await urlBuilder(formData, baseUrl, root);

    fetch(fullUrl)
        .then(res => {
            console.log("📦 Response received:", res);
            return res.text();
        })
        .then(data => {
            console.log("📋 Data processed:", data);
            console.log("✅ Form submitted successfully!");
            // Redirect after successful submission
            window.location.href = "/camera/index.html#/apps";
        })
        .catch(console.error);
});

async function urlBuilder(formData, baseUrl, root) {
    let parameters = "";

    formData.forEach((value, key) => {
        console.log(`Key: ${key}, Value: ${value}`);  // 👈 log each field
        const paramName = root + key;
        parameters += `${encodeURIComponent(paramName)}=${encodeURIComponent(value)}&`;
    });

    // Remove trailing &
    parameters = parameters.slice(0, -1);

    console.log("🔗 Built parameters:", parameters);

    return baseUrl + parameters;
}
document.addEventListener("DOMContentLoaded", function () {
    const closeButton = document.querySelector('.btn.btn-secondary[data-bs-dismiss="modal"]');

    if (closeButton) {
        closeButton.addEventListener('click', function () {
            window.location.href = "/camera/index.html#/apps";
        });
    }
});

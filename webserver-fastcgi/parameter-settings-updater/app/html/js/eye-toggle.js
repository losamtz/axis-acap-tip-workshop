const toggle = document.getElementById('togglePassword');
const password = document.getElementById('password');

toggle.addEventListener('click', () => {
    // Toggle the input type
    const type = password.getAttribute('type') === 'password' ? 'text' : 'password';
    password.setAttribute('type', type);

    // Toggle the eye icon
    toggle.classList.toggle('fa-eye');
    toggle.classList.toggle('fa-eye-slash');
});